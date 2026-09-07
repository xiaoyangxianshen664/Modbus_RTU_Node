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
 *	 bytes[0]等价于：*(bytes + 0)
 * 	 return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1])
 *   等价于return (uint16_t)(((uint16_t)(*(bytes + 0)) << 8U) | *(bytes + 1));
 *
 * 调用示例：
 *   uint16_t register_address = modbus_get_u16(&request[2]);  // 取起始寄存器地址&request[0]为从机地址，&request[1]为功能码
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
 *		结果只有两种：成立 → 返回1  不成立 → 返回0
 *
 * 调用示例：
 *   if (!request_crc_is_valid(request, request_length)) { ... } //CRC 正确：函数返回 1，!1 = 0，不进入错误分支

		串口助手 → STM32
		STM32 收到完整请求帧
		→ 取出帧末尾的两个 CRC 字节
		→ 对前面的内容重新计算 CRC
		→ 比较两者是否一致
		→ 一致：请求帧有效
		→ 不一致：丢弃或返回错误
		因此用串口助手手动发送 Modbus 请求时，需要你自己计算并附加 CRC。
		
		
		而STM32 → 串口助手
		STM32 的协议层会自动计算并添加响应 CRC，你只需要观察完整回复。
		所以 request_crc_is_valid() 不是用来“生成你发送的 CRC”的，
		而是用来检查收到的请求帧对应的 CRC 是否正确

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


	两个应用场景：
		 append_crc() 主要用于生成响应帧的 CRC，也可以用于生成任意需要发送的 Modbus 帧。
		 调用：append_crc(response, 5U);其中 data_length = 5，函数会计算前 5 个字节的 CRC，然后写入
		 response数组为响应帧的缓存区，我们默认给的256字节，够够的！
		 
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
 
		说明：
		write_exception() 不是每次都会执行，它只在请求需要返回 Modbus 异常响应时执行。
		调用链是：
		modbus_rtu_process_request()
    ├─ 0x03 / 0x04 → process_read_request()
    ├─ 0x06 / 0x10 → process_write_request()
    └─ 其他功能码 → 直接调用 write_exception()
		主要有以下几种情况。
		1. 不支持的功能码
		2. 读寄存器数量非法
		3. 读寄存器地址越界
		4. 写请求格式不正确（字节数不等于寄存器数量乘 2 /数量为 0/数量超过寄存器数量最大值/实际帧长度不正确）
		5. 写入地址或数据不合法
		
		其余：
		正常请求        → 生成正常响应
		非法请求        → write_exception() 生成异常响应
		CRC 错误        → 直接丢弃，不调用
		地址不是本机    → 直接丢弃，不调用
		广播写请求      → 执行但不回复
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
 *   process_read_request(request, &registers, response, response_capacity, &len);

		这三个 if 是按顺序对一次读寄存器请求做三层检查：
		1. 检查“数量”是否合法：Modbus 规定一次最多读取 125 个寄存器，所以合法范围是：1 <= count <= 125
			 两种非法情况：count == 0 一个都不读， count > 125次读太多，此时返回异常码 0x03。
		
		2. 检查“地址范围”是否合法，它检查：起始地址是否存在 和 起始地址 + count - 1 是否越界  和 这些寄存器是否允许读取
			 非法情况：寄存器表只有地址：0、1、2、3，主机请求，起始地址 = 3，读取数量 = 2 ，实际要读取：地址 3、4
				地址 4 不存在，于是 modbus_registers_read() 返回错误，第二个 if 被执行，返回异常码：0x02 = 非法数据地址
				这和第一个 if 的区别是：第一个：数量本身不合法 ，第二个：数量合法，但地址范围不合法。
		
		3. 检查响应数组空间是否足够
			 正常读响应的数据部分格式是：地址1字节，功能码1字节，字节计数1字节，读出来的寄存器数据：count × 2 字节
			 所以不含 CRC 的长度是：3 + count × 2
			 如果装不下完整响应，函数返回：MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL
			 这里不返回 Modbus 异常响应，因为这是 STM32 内部缓冲区容量问题，不是主机请求的地址或数据错误

			整体流程可以记成：
			第 1 个 if：请求数量对不对？，返回异常码 0x03
        ↓
			第 2 个 if：寄存器地址能不能读？返回异常码 0x02
        ↓
			第 3 个 if：STM32 的响应数组装不装得下？返回 MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL
        ↓
			读取数据并组装正常响应 ：其中前两个是协议层异常，第三个是程序内部错误。
 * ════════════════════════════════════════════════════════════ */
static modbus_rtu_status_t process_read_request(
    const uint8_t *request,
    modbus_registers_t *registers,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length)
{
    const uint8_t function = request[1];                            		// 提取功能码（0x03 或 0x04）
    const uint16_t register_address = modbus_get_u16(&request[2]);  		// 从请求帧解析起始寄存器地址（大端）
    const uint16_t count = modbus_get_u16(&request[4]);             		// 从请求帧解析要读取的寄存器数量（大端）
    uint16_t values[MODBUS_MAX_READ_REGISTERS];                     		// 定义临时数组存放读出的寄存器值
    const modbus_register_table_t table = function == UINT8_C(0x03) 		// 根据功能码选择寄存器表：0x03→保持，0x04→输入
                                              ? MODBUS_REGISTER_HOLDING
                                              : MODBUS_REGISTER_INPUT;
    modbus_register_status_t register_status; 													// 寄存器读操作返回状态
    size_t response_data_length;              													// 定义响应数据部分长度（不含 CRC）

		
		
		/* 1: 检查读取数量是否合法（必须在 1~125 之间,// 非法则返回异常码 0x03（非法数据值）*/
    if (count == 0U || count > MODBUS_MAX_READ_REGISTERS)
    { 																																	
        return write_exception(request[0], function, UINT8_C(0x03), response,
                               response_capacity, response_length); 		
    }

    register_status = modbus_registers_read( 														// 调用寄存器模型读取数据
        registers,
        table,
        register_address,
        count,
        values);
		/*2：检查读取结果（如起始地址+数量超出表范围），越界则返回异常码 0x02（非法地址）*/
    if (register_status != MODBUS_REGISTER_OK)													
    { 
        return write_exception(request[0], function, UINT8_C(0x02), response,
                               response_capacity, response_length); 		
    }

    response_data_length = 3U + ((size_t)count * 2U); 									// 计算响应数据长度：地址1 + 功能码1 + 字节计数1 + 数据(N×2)
		
		/*3：检查响应缓冲区是否能容纳数据部分 + 2 字节 CRC，空间不足返回错误*/
    if (response_capacity < response_data_length + 2U)
    {                                                										 
        return MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL; 										
    }
		
		/*构造响应帧*/
    response[0] = request[0];            																// 第 0 字节：回显从站地址
    response[1] = function;              																// 第 1 字节：回显功能码
    response[2] = (uint8_t)(count * 2U); 																// 第 2 字节：写入字节计数（寄存器数 × 2）
    for (uint16_t index = 0U; index < count; ++index)
    {
        modbus_put_u16(&response[3U + ((size_t)index * 2U)], values[index]); // 将寄存器值以大端格式逐个写入响应缓冲区
    }
    append_crc(response, response_data_length);   											// 对完整响应数据计算并追加 CRC
    *response_length = response_data_length + 2U; 											// 输出总响应长度（数据 + CRC）
    return MODBUS_RTU_RESPONSE_READY;             											// 返回就绪状态
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

		函数说明：
			
		进入 process_write_request()→ 判断功能码是 0x06 还是 0x10 → 检查对应请求帧格式 → 检查 寄存器地址、数量和待写入值
		
		合法：调用 modbus_registers_write_holding()→ 判断实际写入结果 → 广播不回复，普通请求生成响应

		写入失败
		1. 地址非法
			 - modbus_registers_write_holding() 返回地址错误。
			 - 普通请求回复异常码 0x02。
			 - 广播请求不回复。
		2. 值非法
			 - modbus_registers_write_holding() 返回数据值错误。
			 - 普通请求回复异常码 0x03。
			 - 广播请求不回复。
		3. 广播写失败
			 - 写入失败，但地址是 0x00。
			 - 根据 Modbus 规则，直接返回 MODBUS_RTU_NO_RESPONSE，不发送异常帧。
		
		写入成功：
		1：普通写成功
			回显前 6 字节。
			添加 CRC。
			返回 8 字节响应。 （会有一个if的判断，响应缓冲区不足）写入已经成功，但准备构造普通响应时发现数组小于 8 字节。
			返回 MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL。 这是内部缓冲区错误，不是寄存器写入失败。
		2：广播写成功
			寄存器已经写入
			返回 MODBUS_RTU_NO_RESPONSE。
			不发送任何响应。
		
 * ════════════════════════════════════════════════════════════ */
static modbus_rtu_status_t process_write_request(
    const uint8_t *request,
    size_t request_length,
    modbus_registers_t *registers,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length)
{
    const uint8_t function = request[1];                           										// 提取功能码（0x06 或 0x10）
    const uint16_t register_address = modbus_get_u16(&request[2]); 										// 解析起始寄存器地址（大端）
    uint16_t values[MODBUS_MAX_WRITE_REGISTERS];                   										// 临时存放待写入的寄存器值
    uint16_t count;                                                										// 待写入的寄存器数量
    modbus_register_status_t register_status;                      										// 寄存器写操作返回状态

    if (function == UINT8_C(0x06))																										// ── 处理 0x06 写单个寄存器请求 
    { 
        if (request_length != 8U)																											// 0x06 请求帧固定为 8 字节
        {                                  
            return MODBUS_RTU_NO_RESPONSE; 																						// 长度不符视为坏帧，静默丢弃
        }
        count = 1U;                              																			// 单个寄存器写入，数量固定为 1
        values[0] = modbus_get_u16(&request[4]); 																			// 写入值位于 request[4] 和 request[5]
    }
    else																																							// 处理 0x10 写多个寄存器请求 
    { 
        if (request_length < 9U)																											// 0x10 请求帧至少 9 字节
        { 
            return MODBUS_RTU_NO_RESPONSE;
        }
        const uint8_t byte_count = request[6];                              					// 提取字节计数（偏移 6）
        const uint16_t requested_count = modbus_get_u16(&request[4]);       					// 提取声明的寄存器数量（request[4] 和 request[5]
        const size_t expected_length = 9U + ((size_t)requested_count * 2U); 					// 计算理论帧长 = 9 + 数据字节数
				
				/*
						条件一：数量不能为 0 ，requested_count == 0U，不能要求写 0 个寄存器。
						条件二：数量不能超过最大值equested_count > MODBUS_MAX_WRITE_REGISTERS当前最大写入数量是 123 个寄存器。
						条件三：字节数必须等于寄存器数乘 2，byte_count != requested_count * 2，一个寄存器占 2 字节。
						条件四：实际帧长度必须精确匹配  request_length != expected_length 返回：异常码 0x03：非法数据值
				*/
        if (requested_count == 0U || requested_count > MODBUS_MAX_WRITE_REGISTERS || 	// 数量必须为 1~123
            byte_count != (uint8_t)(requested_count * 2U) ||                         	// 字节计数必须等于数量×2
            request_length != expected_length)
        { // 实际帧长必须与理论帧长一致
            return write_exception(request[0], function, UINT8_C(0x03), response,
                                   response_capacity, response_length);								 // 格式非法返回异常码 0x03
        }

        count = requested_count;
        for (uint16_t index = 0U; index < count; ++index)
        {
            values[index] = modbus_get_u16(&request[7U + ((size_t)index * 2U)]); 				// 从偏移 7 开始逐个提取待写入值（大端）
        }
    }
			

		
    register_status = modbus_registers_write_holding( 																	// 调用寄存器模型执行原子写入
        registers,
        register_address,
        count,
        values);
    if (register_status != MODBUS_REGISTER_OK)																						// 写入失败（地址越界或值非法）
    { 
        const uint8_t exception = register_status == MODBUS_REGISTER_INVALID_ADDRESS
                                      ? UINT8_C(0x02)  																		// 地址非法 → 异常码 0x02
                                      : UINT8_C(0x03); 																		// 值非法 → 异常码 0x03
        if (request[0] == MODBUS_BROADCAST_ADDRESS)
        {
            return MODBUS_RTU_NO_RESPONSE; 																								// 广播请求无论成败都不回复
        }
        return write_exception(request[0], function, exception, response,
                               response_capacity, response_length); 											// 返回对应异常响应
    }

    if (request[0] == MODBUS_BROADCAST_ADDRESS)																						// 广播写成功也不回复
    {
        return MODBUS_RTU_NO_RESPONSE; 
    }
    if (response_capacity < 8U)																														// 响应缓冲区不足 8 字节
    {
        return MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL; 
    }

    for (size_t index = 0U; index < 6U; ++index)																					 // 写响应：原样回传请求前 6 字节（地址+功能码+起始地址+数量/值）
    { 
        response[index] = request[index];																									 // 对前 6 字节计算并追加 CRC
    }
    append_crc(response, 6U);         
    *response_length = 8U;            																										// 写响应固定 8 字节
    return MODBUS_RTU_RESPONSE_READY; 																										// 返回就绪状态
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
 
	ModbusTask
    ↓
	modbus_rtu_process_request()
    ├─ 检查参数
    ├─ 检查帧长度
    ├─ 检查 CRC
    ├─ 检查从站地址
    ├─ 判断广播规则
    └─ 按功能码分发
        ├─ 0x03 / 0x04 → process_read_request()
        ├─ 0x06 / 0x10 → process_write_request()
        └─ 其他功能码 → write_exception()
				
   检查的流程是：
	 参数有效？
			↓
	 长度正确？
			↓
	 CRC 正确？
			↓
	 地址是本机或广播？
			↓
	 如果是广播，功能码是否为写操作？
			↓
	 进入读/写/异常功能码分支

 
 * ════════════════════════════════════════════════════════════ */
modbus_rtu_status_t modbus_rtu_process_request(
    const uint8_t *request,        																			/* 输入：指向接收到的原始请求缓冲区（即一帧完整的 Modbus RTU ADU，含地址+PDU+CRC） */
    size_t request_length,         																			/* 输入：请求 ADU 的实际字节数（用于长度合法性校验和 CRC 计算范围） */
    modbus_registers_t *registers, 																			/* 输入/输出：指向从站寄存器映射表；读请求时从中取数据，写请求时往里写数据 */
    uint8_t *response,             																			/* 输出：指向调用方提供的响应缓冲区，函数内会构建好要发回的响应 ADU（含地址+PDU+CRC） */
    size_t response_capacity,      																			/* 输入：响应缓冲区的最大容量（防止构建响应时越界写溢出） */
    size_t *response_length        																			/* 输出：函数返回时，由本函数写入实际响应 ADU 的字节数；出错时保持为 0 */
)
{
    uint8_t slave_address; 																							/* 从站地址：解析自请求 ADU 第 1 字节（request[0]），用于匹配本机地址或识别广播 */
    uint8_t function;      																							/* 功能码：解析自请求 ADU 第 2 字节（request[1]），用于分发读/写操作或生成异常响应 */
		
	
		/*1. 空指针检查，函数需要四个有效地址：request：输入请求帧，registers：寄存器表，response：输出响应缓冲区 ，response_length：输出响应长度*/
    if (request == NULL || registers == NULL || response == NULL || response_length == NULL)
    {
        return MODBUS_RTU_INVALID_ARGUMENT; 														
    }
		
		/*2.先清零响应长度，函数开始时先把输出长度设为 0，表示：当前还没有生成有效响应
		  表示如果后面任意检查失败，调用者看到的长度仍然是 0，不会误把旧数据当成新响应发送
		*/
    *response_length = 0U; 																							
		
		/*3.请求帧长度检查，检查请求帧是否在允许范围内：最小：4 字节最大：256 字节，长度不合法，静默丢弃*/
    if (request_length < 4U || request_length > MODBUS_MAX_ADU_SIZE)
    {                                  																	
        return MODBUS_RTU_NO_RESPONSE; 																	
    }
		
		/*4. 提取地址和功能码，得到：slave_address = 0x01，function = 0x03或0x04 */
    slave_address = request[0]; 																				
    function = request[1];      																				
		
		/*5. CRC 校验，CRC 正确 → 返回 1，CRC 错误 → 返回 0
			CRC 正确：!1 = 0，不进入 if
			CRC 错误：!0 = 1，进入 if ，CRC 错误，静默丢弃
		*/
    if (!request_crc_is_valid(request, request_length))
    {                                  																	
        return MODBUS_RTU_NO_RESPONSE; 																	
    }
		
		/*6. 检查从站地址，这段使用的是 &&，意思是：既不是本机地址并且也不是广播地址，静默丢弃*/
    if (slave_address != MODBUS_DEFAULT_SLAVE_ADDRESS && slave_address != MODBUS_BROADCAST_ADDRESS)
    {
        return MODBUS_RTU_NO_RESPONSE; 																	
    }
		
		/*7. 广播功能码限制，Modbus 广播只允许写操作：当条件地址是广播 0x00并且功能码不是 0x06并且功能码不是 0x10进入if
				 这是广播读请求，不允许执行，也不回复
		*/
		
    if (slave_address == MODBUS_BROADCAST_ADDRESS && function != UINT8_C(0x06) && 
        function != UINT8_C(0x10))																		
    {		
        return MODBUS_RTU_NO_RESPONSE; 																
    }
		
		
		/*1：分发读寄存器功能码0x03/0x04 */
		
    if (function == UINT8_C(0x03) || function == UINT8_C(0x04))					// ⑥ 分发读寄存器功能码
    { 
        if (slave_address == MODBUS_BROADCAST_ADDRESS || request_length != 8U)
        { 																															// 读请求必须是单播且帧长固定 8 字节
            return MODBUS_RTU_NO_RESPONSE;
        }
        return process_read_request(request, registers, response, response_capacity,
                                    response_length); 									// 调用读请求处理函数
    }
		
		/*2：分发写寄存器功能码0x06/0x10 */
		
    if (function == UINT8_C(0x06) || function == UINT8_C(0x10))					// ⑥ 分发写寄存器功能码，传入完整请求帧，供写处理函数解析
    {                                                                         
        return process_write_request(request, request_length, registers, response,  
                                    response_capacity, response_length);// 调用写请求处理函数
    }
		
		/*3：不支持的功能码 */

    return write_exception(slave_address, function, UINT8_C(0x01), response, // ⑥ 其他功能码不支持
                           response_capacity, response_length);              // 返回异常码 0x01（非法功能）
}
