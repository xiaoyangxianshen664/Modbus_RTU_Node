#include "modbus_rtu.h"

#include "modbus_crc16.h"

/* ════════════════════════════════════════════════════════════
 * modbus_get_u16 — 从两个连续字节读出 16 位大端数值
 *
 * 原型：uint16_t modbus_get_u16(const uint8_t *bytes)
 *       bytes — [输入] 指向两个连续字节，高字节在前（大端序）
 * 返值：拼出来的 16 位值
 *
 * 原理：
 *   Modbus 线上 16 位值按「高字节在前」传输。
 *   例如字节 [0x12, 0x34] → 0x1234。
 *   就是把高字节左移 8 位，再和低字节拼起来。
 *
 * 调用示例：
 *   uint16_t register_address = modbus_get_u16(&request[2]);  // 取起始寄存器地址
 * ════════════════════════════════════════════════════════════ */
uint16_t modbus_get_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]); // 高字节左移 8 位后与低字节拼接成 16 位值
}

/* ════════════════════════════════════════════════════════════
 * modbus_put_u16 — 把一个 16 位值拆成两个大端字节写入缓冲区
 *
 * 原型：void modbus_put_u16(uint8_t *bytes, uint16_t value)
 *       bytes — [输出] 至少两个字节的缓冲区
 *       value — [输入] 要拆分的 16 位值
 * 返值：无
 *
 * 原理：
 *   与 modbus_get_u16 相反：value 0x1234 → bytes[0]=0x12, bytes[1]=0x34。
 *   高字节 = 右移 8 位；低字节 = 与 0x00FF 相与。
 *
 * 调用示例：
 *   modbus_put_u16(&response[3], register_value);  // 往响应里写一个寄存器值
 * ════════════════════════════════════════════════════════════ */
void modbus_put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);              // 提取高字节写入缓冲区
    bytes[1] = (uint8_t)(value & UINT16_C(0x00FF)); // 提取低字节写入缓冲区
}

/* ════════════════════════════════════════════════════════════
 * request_crc_is_valid — 校验请求帧末尾的 CRC 是否正确
 *
 * 原型：static int request_crc_is_valid(const uint8_t *request, size_t length)
 *       request — [输入] 完整请求帧（含地址/PDU/CRC）
 *       length  — [输入] 请求帧总字节数
 * 返值：1 = CRC 正确，0 = CRC 错误
 *
 * 原理：
 *   对「除最后 2 字节 CRC 外的所有数据」重算一次 CRC，
 *   再和帧尾那 2 字节（低字节在前）逐一比较。
 *   算出来一样 = 传输没损坏。
 *
 * 调用示例：
 *   if (!request_crc_is_valid(request, request_length)) { ... }
 * ════════════════════════════════════════════════════════════ */
static int request_crc_is_valid(const uint8_t *request, size_t length)
{
    if (request == NULL || length < 2U)
    {             // 指针为空或帧长不足 2 字节（无法容纳 CRC）
        return 0; // 返回 0 表示校验失败
    }

    const uint16_t crc = modbus_crc16(request, length - 2U); // 对除末尾 2 字节外的数据重算 CRC

    return request[length - 2U] == (uint8_t)(crc & UINT16_C(0x00FF)) && // 比较帧尾低字节与计算结果的低字节
           request[length - 1U] == (uint8_t)(crc >> 8U);                // 比较帧尾高字节与计算结果的高字节
}

/* ════════════════════════════════════════════════════════════
 * append_crc — 计算 CRC 并追加到帧末尾
 *
 * 原型：static void append_crc(uint8_t *frame, size_t data_length)
 *       frame       — [输入输出] 帧缓冲区（末尾要留 2 字节空位）
 *       data_length — [输入] 帧里已写入的数据字节数（不含 CRC）
 * 返值：无
 *
 * 原理：
 *   对前 data_length 个字节算 CRC，把低字节、高字节依次写到末尾。
 *   Modbus RTU 规定 CRC 低字节在前。
 *
 * 调用示例：
 *   append_crc(response, 3U);  // 给 3 字节的异常响应补上 CRC
 * ════════════════════════════════════════════════════════════ */
static void append_crc(uint8_t *frame, size_t data_length)
{
    const uint16_t crc = modbus_crc16(frame, data_length);  // 计算帧数据的 CRC16 校验值
    frame[data_length] = (uint8_t)(crc & UINT16_C(0x00FF)); // 低字节写入帧尾（Modbus 规定低字节在前）
    frame[data_length + 1U] = (uint8_t)(crc >> 8U);         // 高字节写入下一字节
}

/* ════════════════════════════════════════════════════════════
 * write_exception — 构造一条 Modbus 异常响应
 *
 * 原型：static modbus_rtu_status_t write_exception(uint8_t slave_address, uint8_t function,
 *         uint8_t exception, uint8_t *response, size_t response_capacity, size_t *response_length)
 *       slave_address     — [输入] 从站地址
 *       function          — [输入] 出错的请求功能码
 *       exception         — [输入] 异常码（01/02/03）
 *       response          — [输出] 响应缓冲区
 *       response_capacity — [输入] 响应缓冲区容量
 *       response_length   — [输出] 实际响应长度
 * 返值：READY（成功）/ BUFFER_TOO_SMALL（缓冲区不够）
 *
 * 原理：
 *   异常响应固定 5 字节：地址 + (功能码|0x80) + 异常码 + CRC(2)。
 *   功能码最高位置 1，表示"这是异常响应"。
 *
 * 调用示例：
 *   write_exception(request[0], function, 0x01, response, cap, &len);
 * ════════════════════════════════════════════════════════════ */
static modbus_rtu_status_t write_exception( // write_exception — 构造一条 Modbus 异常响应
    uint8_t slave_address,                  // 从站地址
    uint8_t function,                       // 出错的请求功能码
    uint8_t exception,                      // 异常码（01/02/03）
    uint8_t *response,                      // 响应缓冲区
    size_t response_capacity,               // 响应缓冲区容量
    size_t *response_length)                // 实际响应长度
{
    if (response_capacity < 5U)
    {                                                // 检查缓冲区是否足以容纳 5 字节异常响应
        return MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL; // 空间不足，返回缓冲区过小错误
    }

    response[0] = slave_address;               // 第 0 字节：写入从站地址
    response[1] = (uint8_t)(function | 0x80U); // 第 1 字节：功能码最高位置 1，标记为异常响应
    response[2] = exception;                   // 第 2 字节：写入异常码
    append_crc(response, 3U);                  // 对前 3 字节计算 CRC 并追加到帧尾

    *response_length = 5U;            // 设置实际响应长度为 5 字节（地址+功能码+异常码+CRC）
    return MODBUS_RTU_RESPONSE_READY; // 构造完成，返回就绪状态
}

/* ════════════════════════════════════════════════════════════
 * process_read_request — 处理 0x03 / 0x04 读寄存器请求
 *
 * 原型：static modbus_rtu_status_t process_read_request(const uint8_t *request,
 *         modbus_registers_t *registers, uint8_t *response,
 *         size_t response_capacity, size_t *response_length)
 * 返值：READY（成功）/ BUFFER_TOO_SMALL / 或经 write_exception 返回
 *
 * 原理：
 *   读请求帧：地址 + 功能码 + 起始地址(2) + 数量(2) + CRC(2)。
 *   ① 数量超范围 → 异常码 03（非法数据值）
 *   ② 地址越界   → 异常码 02（非法地址）
 *   ③ 正常：响应 = 地址 + 功能码 + 字节计数 + 数据 + CRC
 *
 * 调用示例：
 *   process_read_request(request, &registers, response, cap, &len);
 * ════════════════════════════════════════════════════════════ */
static modbus_rtu_status_t process_read_request(
    const uint8_t *request,
    modbus_registers_t *registers,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length)
{
    const uint8_t function = request[1];                            // 提取功能码（0x03 或 0x04）
    const uint16_t register_address = modbus_get_u16(&request[2]);  // 从请求帧解析起始寄存器地址（大端）
    const uint16_t count = modbus_get_u16(&request[4]);             // 从请求帧解析要读取的寄存器数量（大端）
    uint16_t values[MODBUS_MAX_READ_REGISTERS];                     // 定义临时数组存放读出的寄存器值
    const modbus_register_table_t table = function == UINT8_C(0x03) // 根据功能码选择寄存器表：0x03→保持，0x04→输入
                                              ? MODBUS_REGISTER_HOLDING
                                              : MODBUS_REGISTER_INPUT;
    modbus_register_status_t register_status; // 寄存器读操作返回状态
    size_t response_data_length;              // 响应数据部分长度（不含 CRC）

    if (count == 0U || count > MODBUS_MAX_READ_REGISTERS)
    { // 检查读取数量是否合法（必须在 1~125 之间）
        return write_exception(request[0], function, UINT8_C(0x03), response,
                               response_capacity, response_length); // 非法则返回异常码 0x03（非法数据值）
    }

    register_status = modbus_registers_read( // 调用寄存器模型读取数据
        registers,
        table,
        register_address,
        count,
        values);
    if (register_status != MODBUS_REGISTER_OK)
    { // 检查读取结果（如起始地址+数量超出表范围）
        return write_exception(request[0], function, UINT8_C(0x02), response,
                               response_capacity, response_length); // 越界则返回异常码 0x02（非法地址）
    }

    response_data_length = 3U + ((size_t)count * 2U); // 计算响应数据长度：地址1 + 功能码1 + 字节计数1 + 数据(N×2)
    if (response_capacity < response_data_length + 2U)
    {                                                // 检查响应缓冲区是否能容纳数据部分 + 2 字节 CRC
        return MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL; // 空间不足返回错误
    }

    response[0] = request[0];            // 第 0 字节：回显从站地址
    response[1] = function;              // 第 1 字节：回显功能码
    response[2] = (uint8_t)(count * 2U); // 第 2 字节：写入字节计数（寄存器数 × 2）
    for (uint16_t index = 0U; index < count; ++index)
    {
        modbus_put_u16(&response[3U + ((size_t)index * 2U)], values[index]); // 将寄存器值以大端格式逐个写入响应缓冲区
    }
    append_crc(response, response_data_length);   // 对完整响应数据计算并追加 CRC
    *response_length = response_data_length + 2U; // 输出总响应长度（数据 + CRC）
    return MODBUS_RTU_RESPONSE_READY;             // 返回就绪状态
}

/* ════════════════════════════════════════════════════════════
 * process_write_request — 处理 0x06 / 0x10 写寄存器请求
 *
 * 原型：static modbus_rtu_status_t process_write_request(const uint8_t *request,
 *         size_t request_length, modbus_registers_t *registers, uint8_t *response,
 *         size_t response_capacity, size_t *response_length)
 * 返值：READY（成功）/ NO_RESPONSE（广播或坏帧）/ BUFFER_TOO_SMALL
 *
 * 原理：
 *   0x06 写单个：地址 + 功能码 + 寄存器地址(2) + 值(2) + CRC，固定 8 字节。
 *   0x10 写多个：地址 + 功能码 + 起始地址(2) + 数量(2) + 字节计数(1) + 数据(N) + CRC。
 *   广播（地址 0）的写请求：执行写入但【不回复】。
 *
 * 调用示例：
 *   process_write_request(request, request_length, &registers, response, cap, &len);
 * ════════════════════════════════════════════════════════════ */
static modbus_rtu_status_t process_write_request(
    const uint8_t *request,
    size_t request_length,
    modbus_registers_t *registers,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length)
{
    const uint8_t function = request[1];                           // 提取功能码（0x06 或 0x10）
    const uint16_t register_address = modbus_get_u16(&request[2]); // 解析起始寄存器地址（大端）
    uint16_t values[MODBUS_MAX_WRITE_REGISTERS];                   // 临时存放待写入的寄存器值
    uint16_t count;                                                // 待写入的寄存器数量
    modbus_register_status_t register_status;                      // 寄存器写操作返回状态

    if (function == UINT8_C(0x06))
    { // ── 处理 0x06 写单个寄存器请求 ──
        if (request_length != 8U)
        {                                  // 0x06 请求帧固定为 8 字节
            return MODBUS_RTU_NO_RESPONSE; // 长度不符视为坏帧，静默丢弃
        }
        count = 1U;                              // 单个寄存器写入，数量固定为 1
        values[0] = modbus_get_u16(&request[4]); // 从请求帧偏移 4 提取要写入的值（大端）
    }
    else
    { // ── 处理 0x10 写多个寄存器请求 ──
        if (request_length < 9U)
        { // 0x10 请求帧至少 9 字节
            return MODBUS_RTU_NO_RESPONSE;
        }
        const uint8_t byte_count = request[6];                              // 提取字节计数（偏移 6）
        const uint16_t requested_count = modbus_get_u16(&request[4]);       // 提取声明的寄存器数量（偏移 4~5）
        const size_t expected_length = 9U + ((size_t)requested_count * 2U); // 计算理论帧长 = 9 + 数据字节数

        if (requested_count == 0U || requested_count > MODBUS_MAX_WRITE_REGISTERS || // 数量必须为 1~123
            byte_count != (uint8_t)(requested_count * 2U) ||                         // 字节计数必须等于数量×2
            request_length != expected_length)
        { // 实际帧长必须与理论帧长一致
            return write_exception(request[0], function, UINT8_C(0x03), response,
                                   response_capacity, response_length); // 格式非法返回异常码 0x03
        }

        count = requested_count;
        for (uint16_t index = 0U; index < count; ++index)
        {
            values[index] = modbus_get_u16(&request[7U + ((size_t)index * 2U)]); // 从偏移 7 开始逐个提取待写入值（大端）
        }
    }

    register_status = modbus_registers_write_holding( // 调用寄存器模型执行原子写入
        registers,
        register_address,
        count,
        values);
    if (register_status != MODBUS_REGISTER_OK)
    { // 写入失败（地址越界或值非法）
        const uint8_t exception = register_status == MODBUS_REGISTER_INVALID_ADDRESS
                                      ? UINT8_C(0x02)  // 地址非法 → 异常码 0x02
                                      : UINT8_C(0x03); // 值非法 → 异常码 0x03
        if (request[0] == MODBUS_BROADCAST_ADDRESS)
        {
            return MODBUS_RTU_NO_RESPONSE; // 广播请求无论成败都不回复
        }
        return write_exception(request[0], function, exception, response,
                               response_capacity, response_length); // 返回对应异常响应
    }

    if (request[0] == MODBUS_BROADCAST_ADDRESS)
    {
        return MODBUS_RTU_NO_RESPONSE; // 广播写成功也不回复
    }
    if (response_capacity < 8U)
    {
        return MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL; // 响应缓冲区不足 8 字节
    }

    for (size_t index = 0U; index < 6U; ++index)
    { // 写响应：原样回传请求前 6 字节（地址+功能码+起始地址+数量/值）
        response[index] = request[index];
    }
    append_crc(response, 6U);         // 对前 6 字节计算并追加 CRC
    *response_length = 8U;            // 写响应固定 8 字节
    return MODBUS_RTU_RESPONSE_READY; // 返回就绪状态
}

/* ════════════════════════════════════════════════════════════
 * modbus_rtu_process_request — 协议主入口：校验并处理一帧 Modbus 请求
 *
 * 原型：modbus_rtu_status_t modbus_rtu_process_request(const uint8_t *request,
 *         size_t request_length, modbus_registers_t *registers, uint8_t *response,
 *         size_t response_capacity, size_t *response_length)
 * 返值：READY（要回复）/ NO_RESPONSE（静默丢弃）/ INVALID_ARGUMENT / BUFFER_TOO_SMALL
 *
 * 处理流程（顺序很重要）：
 *   ① 指针空检查 → INVALID_ARGUMENT
 *   ② 帧长检查（4~256）→ 不合法静默丢弃
 *   ③ CRC 校验 → 错就静默丢弃
 *   ④ 地址检查：不是本机也不是广播 → 静默丢弃
 *   ⑤ 广播限制：广播只允许写（0x06/0x10），读广播 → 静默丢弃
 *   ⑥ 分派：0x03/0x04 → 读；0x06/0x10 → 写；其他 → 异常码 01（非法功能码）
 *
 * 调用示例：
 *   modbus_rtu_process_request(frame, frame_len, &registers, resp, sizeof(resp), &resp_len);
 * ════════════════════════════════════════════════════════════ */
modbus_rtu_status_t modbus_rtu_process_request(
    const uint8_t *request,        /* 输入：指向接收到的原始请求缓冲区（即一帧完整的 Modbus RTU ADU，含地址+PDU+CRC） */
    size_t request_length,         /* 输入：请求 ADU 的实际字节数（用于长度合法性校验和 CRC 计算范围） */
    modbus_registers_t *registers, /* 输入/输出：指向从站寄存器映射表；读请求时从中取数据，写请求时往里写数据 */
    uint8_t *response,             /* 输出：指向调用方提供的响应缓冲区，函数内会构建好要发回的响应 ADU（含地址+PDU+CRC） */
    size_t response_capacity,      /* 输入：响应缓冲区的最大容量（防止构建响应时越界写溢出） */
    size_t *response_length        /* 输出：函数返回时，由本函数写入实际响应 ADU 的字节数；出错时保持为 0 */
)
{
    uint8_t slave_address; /* 从站地址：解析自请求 ADU 第 1 字节（request[0]），用于匹配本机地址或识别广播 */
    uint8_t function;      /* 功能码：解析自请求 ADU 第 2 字节（request[1]），用于分发读/写操作或生成异常响应 */

    if (request == NULL || registers == NULL || response == NULL || response_length == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT; // ① 空指针检查，参数非法直接返回
    }
    *response_length = 0U; // 先清零响应长度，确保出错时输出为 0
    if (request_length < 4U || request_length > MODBUS_MAX_ADU_SIZE)
    {                                  // ② 帧长合法性检查（最小 4 字节，最大 256 字节）
        return MODBUS_RTU_NO_RESPONSE; // 长度不合法，静默丢弃
    }

    slave_address = request[0]; // 提取从站地址
    function = request[1];      // 提取功能码
    if (!request_crc_is_valid(request, request_length))
    {                                  // ③ 校验请求帧 CRC 是否正确
        return MODBUS_RTU_NO_RESPONSE; // CRC 错误，静默丢弃
    }
    if (slave_address != MODBUS_DEFAULT_SLAVE_ADDRESS && slave_address != MODBUS_BROADCAST_ADDRESS)
    {
        return MODBUS_RTU_NO_RESPONSE; // ④ 地址非本机且非广播，静默丢弃
    }
    if (slave_address == MODBUS_BROADCAST_ADDRESS && function != UINT8_C(0x06) && // ⑤ 广播只允许写操作
        function != UINT8_C(0x10))
    {
        return MODBUS_RTU_NO_RESPONSE; // 广播读请求，不回复
    }

    if (function == UINT8_C(0x03) || function == UINT8_C(0x04))
    { // ⑥ 分发读寄存器功能码
        if (slave_address == MODBUS_BROADCAST_ADDRESS || request_length != 8U)
        { // 读请求必须是单播且帧长固定 8 字节
            return MODBUS_RTU_NO_RESPONSE;
        }
        return process_read_request(request, registers, response, response_capacity,
                                    response_length); // 调用读请求处理函数
    }
    if (function == UINT8_C(0x06) || function == UINT8_C(0x10))
    {                                                                           // ⑥ 分发写寄存器功能码
        return process_write_request(request, request_length, registers, response, // 传入完整请求帧，供写处理函数解析
                                     response_capacity, response_length);            // 调用写请求处理函数
    }

    return write_exception(slave_address, function, UINT8_C(0x01), response, // ⑥ 其他功能码不支持
                           response_capacity, response_length);              // 返回异常码 0x01（非法功能）
}
