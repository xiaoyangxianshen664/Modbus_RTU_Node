#include "modbus_crc16.h"
#include "modbus_rtu.h"

#include <stdio.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════
 * TEST_ASSERT_EQ — 比较期望值与实际值，不等则打印并返回失败
 * 不依赖标准 assert()，避免定义 NDEBUG 后测试失效。
 * ════════════════════════════════════════════════════════════ */
#define TEST_ASSERT_EQ(expected, actual)                              \
    do                                                                \
    {                                                                 \
        const unsigned expected_value = (unsigned)(expected);         \
        const unsigned actual_value = (unsigned)(actual);             \
        if (expected_value != actual_value)                           \
        {                                                             \
            printf("FAIL %s:%d: expected 0x%X, actual 0x%X\n",        \
                   __FILE__, __LINE__, expected_value, actual_value); \
            return 1;                                                 \
        }                                                             \
    } while (0)

/* ════════════════════════════════════════════════════════════
 * finalize_frame — 给测试帧末尾补上 CRC，返回完整帧长
 *
 * 原型：static size_t finalize_frame(uint8_t *frame, size_t data_length)
 *       frame       — [输入输出] 帧缓冲区
 *       data_length — [输入] 已填的数据字节数（不含 CRC）
 * 返值：补完 CRC 后的总长度
 *
 * 用途：测试里手工组帧后，用它把 CRC 算好附上，模拟"线上收到的完整帧"。
 *
 * 调用示例：
 *   modbus_rtu_process_request(request, finalize_frame(request, 6U), ...);
 * ════════════════════════════════════════════════════════════ */
static size_t finalize_frame(uint8_t *frame, size_t data_length)
{
    const uint16_t crc = modbus_crc16(frame, data_length);  // 对数据段算 CRC
    frame[data_length] = (uint8_t)(crc & UINT16_C(0x00FF)); // 低字节在前
    frame[data_length + 1U] = (uint8_t)(crc >> 8U);         // 高字节在后
    return data_length + 2U;
}

/* ════════════════════════════════════════════════════════════
 * assert_response_crc — 校验响应帧末尾的 CRC 是否正确
 *
 * 原型：static int assert_response_crc(const uint8_t *frame, size_t length)
 *       frame  — [输入] 响应帧
 *       length — [输入] 响应帧总长
 * 返值：0 = CRC 正确；CRC 错误时触发 TEST_ASSERT_EQ 返回 1
 *
 * 用途：确认从站生成的响应 CRC 没算错（防止组帧 bug）。
 *
 * 调用示例：
 *   TEST_ASSERT_EQ(0, assert_response_crc(response, response_length));
 * ════════════════════════════════════════════════════════════ */
static int assert_response_crc(const uint8_t *frame, size_t length)
{
    const uint16_t expected = modbus_crc16(frame, length - 2U); // 重算 CRC
    const uint16_t actual = (uint16_t)(frame[length - 2U] |     // 从帧尾读出 CRC（低字节在前）
                                       ((uint16_t)frame[length - 1U] << 8U));
    TEST_ASSERT_EQ(expected, actual); // 两者必须一致
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * test_read_functions — 测 0x03 / 0x04 读寄存器
 *
 * 覆盖：
 *   0x03 读保持寄存器 → 返回默认采样周期 1000
 *   0x04 读输入寄存器 → 返回刚更新的采集值 0x1234
 * ════════════════════════════════════════════════════════════ */
static int test_read_functions(void)
{
    modbus_registers_t registers;
    uint8_t request[8];
    uint8_t response[MODBUS_MAX_ADU_SIZE];
    size_t response_length;

    modbus_registers_init(&registers);
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,
                   modbus_registers_update_input(&registers, 0U, UINT16_C(0x1234))); // 输入寄存器 0 = 0x1234

    request[0] = 1U;
    request[1] = 0x03U;
    request[2] = 0U;
    request[3] = 0U; // 读保持寄存器，起始地址 0
    request[4] = 0U;
    request[5] = 1U; // 数量 1
    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    TEST_ASSERT_EQ(7U, response_length);                          // 读 1 个：1+1+1+2+2=7
    TEST_ASSERT_EQ(0x03U, response[1]);                           // 功能码原样回 0x03
    TEST_ASSERT_EQ(2U, response[2]);                              // 字节计数 = 1 个寄存器 ×2
    TEST_ASSERT_EQ(UINT16_C(1000), modbus_get_u16(&response[3])); // 保持寄存器 0 的默认值 1000
    TEST_ASSERT_EQ(0, assert_response_crc(response, response_length));

    request[1] = 0x04U; // 改成读输入寄存器
    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    TEST_ASSERT_EQ(0x04U, response[1]);                             // 功能码 0x04
    TEST_ASSERT_EQ(UINT16_C(0x1234), modbus_get_u16(&response[3])); // 读到采集值 0x1234
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * test_write_functions — 测 0x06 / 0x10 写保持寄存器
 *
 * 覆盖：
 *   0x06 写单个 → 保持寄存器 1 变成 42，响应回显请求
 *   0x10 写多个 → 保持寄存器 1/2 变成 77/1
 * ════════════════════════════════════════════════════════════ */
static int test_write_functions(void)
{
    modbus_registers_t registers;          // 寄存器地址映射
    uint8_t request[16];                   // 请求帧缓冲区（ADU）
    uint8_t response[MODBUS_MAX_ADU_SIZE]; // 响应帧缓冲区
    size_t response_length;                // 响应帧实际长度

    modbus_registers_init(&registers); // 初始化寄存器组，清零所有保持/输入寄存器

    /* ── 构造 0x06 写单个保持寄存器请求 ── */
    request[0] = 1U;    // 从站地址 = 1
    request[1] = 0x06U; // 功能码 0x06：写单个保持寄存器
    request[2] = 0U;    // 目标寄存器地址高字节 → 0x00
    request[3] = 1U;    // 目标寄存器地址低字节 → 0x0001（寄存器 1）
    request[4] = 0U;    // 写入值高字节 → 0x00
    request[5] = 42U;   // 写入值低字节 → 42

    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY, // 断言：处理函数返回"有待发送响应"状态
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));

    TEST_ASSERT_EQ(8U, response_length);                               // 写单个响应固定 8 字节（地址+功能码+地址+值+CRC）
    TEST_ASSERT_EQ(UINT16_C(42), registers.holding[1]);                // 断言：holding[1] 已成功写入 42
    TEST_ASSERT_EQ(0, assert_response_crc(response, response_length)); // 断言：响应帧 CRC 校验通过

    /* ── 切换为 0x10 写多个保持寄存器请求 ── */
    request[1] = 0x10U; // 功能码 0x10：写多个保持寄存器
    request[4] = 0U;    // 寄存器数量高字节 → 0x00
    request[5] = 2U;    // 寄存器数量低字节 → 2（连续写 2 个寄存器）
    request[6] = 4U;    // 字节计数 → 4（2 个寄存器 × 2 字节/个）
    request[7] = 0U;    // 第 1 个寄存器值高字节 → 0x00
    request[8] = 77U;   // 第 1 个寄存器值低字节 → 77
    request[9] = 0U;    // 第 2 个寄存器值高字节 → 0x00
    request[10] = 1U;   // 第 2 个寄存器值低字节 → 1

    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY, // 断言：处理函数正确响应写多个请求
                   modbus_rtu_process_request(request, finalize_frame(request, 11U),
                                              &registers, response, sizeof(response),
                                              &response_length));

    TEST_ASSERT_EQ(UINT16_C(77), registers.holding[1]);                // 断言：holding[1] 被写为 77
    TEST_ASSERT_EQ(UINT16_C(1), registers.holding[2]);                 // 断言：holding[2] 被写为 1
    TEST_ASSERT_EQ(0, assert_response_crc(response, response_length)); // 断言：响应帧 CRC 校验通过

    return 0; // 测试通过，返回 0
}

/* ════════════════════════════════════════════════════════════
 * test_exceptions_and_silent_frames — 测异常响应与静默丢弃
 *
 * 覆盖：
 *   非法功能码 0x7F → 异常 01
 *   读地址越界（起始 3 读 2 个，超 4 个上限）→ 异常 02
 *   非本机地址（2）→ 静默丢弃
 *   CRC 错误 → 静默丢弃
 *   广播读（地址 0 + 0x04）→ 静默丢弃
 *   广播写（地址 0 + 0x06）→ 执行写入但不回复
 * ════════════════════════════════════════════════════════════ */
static int test_exceptions_and_silent_frames(void)
{
    modbus_registers_t registers;
    uint8_t request[16];
    uint8_t response[MODBUS_MAX_ADU_SIZE];
    size_t response_length;

    modbus_registers_init(&registers);
    request[0] = 1U;
    request[1] = 0x7FU;
    request[2] = 0U;
    request[3] = 0U; // 非法功能码 0x7F
    request[4] = 0U;
    request[5] = 0U;
    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    TEST_ASSERT_EQ(5U, response_length); // 异常响应 5 字节
    TEST_ASSERT_EQ(0xFFU, response[1]);  // 0x7F | 0x80 = 0xFF
    TEST_ASSERT_EQ(0x01U, response[2]);  // 异常码 01（非法功能码）

    request[1] = 0x03U;
    request[2] = 0U;
    request[3] = 3U;
    request[4] = 0U;
    request[5] = 2U; // 起始 3 读 2 个（越界）
    TEST_ASSERT_EQ(MODBUS_RTU_RESPONSE_READY,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    TEST_ASSERT_EQ(0x02U, response[2]); // 异常码 02（非法地址）

    request[0] = 2U; // 非本机地址
    TEST_ASSERT_EQ(MODBUS_RTU_NO_RESPONSE,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));

    request[0] = 1U;
    request[6] ^= 1U; // 故意破坏 CRC
    TEST_ASSERT_EQ(MODBUS_RTU_NO_RESPONSE,
                   modbus_rtu_process_request(request, 8U, &registers, response,
                                              sizeof(response), &response_length));

    request[0] = 0U;
    request[1] = 0x04U; // 广播读 → 静默丢弃
    TEST_ASSERT_EQ(MODBUS_RTU_NO_RESPONSE,
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));

    request[1] = 0x06U;
    request[2] = 0U;
    request[3] = 1U;
    request[4] = 0U;
    request[5] = 55U;                      // 广播写：地址 0 + 0x06
    TEST_ASSERT_EQ(MODBUS_RTU_NO_RESPONSE, // 广播写：执行但不回复
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    TEST_ASSERT_EQ(UINT16_C(55), registers.holding[1]); // 但写入确实生效了

    request[0] = 1U;
    request[1] = 0x10U; // 恢复地址 1，测 0x10 写越界（起始 0 写 1 个合法，仅占位）
    request[2] = 0U;
    request[3] = 0U;
    request[4] = 0U;
    request[5] = 1U;
    TEST_ASSERT_EQ(MODBUS_RTU_NO_RESPONSE, // 长度不足（0x10 至少 9 字节）→ 静默丢弃
                   modbus_rtu_process_request(request, finalize_frame(request, 6U),
                                              &registers, response, sizeof(response),
                                              &response_length));
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * test_too_short_requests — 验证不足 4 字节的 ADU 会被安全拒绝
 *
 * 覆盖长度：
 *   0、1、2、3 字节
 *
 * 预期：
 *   返回 MODBUS_RTU_NO_RESPONSE
 *   response_length 保持为 0
 * ════════════════════════════════════════════════════════════ */
static int test_too_short_requests(void)
{
    modbus_registers_t registers;
    const uint8_t request[3] = {
        UINT8_C(0x01),
        UINT8_C(0x03),
        UINT8_C(0x00),
    };
    uint8_t response[MODBUS_MAX_ADU_SIZE];
    size_t response_length;

    modbus_registers_init(&registers);

    for (size_t length = 0U; length < 4U; ++length)
    {
        response_length = 99U; // 确认被函数重新清零

        TEST_ASSERT_EQ(
            MODBUS_RTU_NO_RESPONSE,
            modbus_rtu_process_request(
                request,
                length,
                &registers,
                response,
                sizeof(response),
                &response_length));

        TEST_ASSERT_EQ(0U, response_length);
    }

    return 0;
}

/* ════════════════════════════════════════════════════════════
 * main — 依次跑四个测试函数，任一失败即返回 1
 * 返回：0 = 全部通过，1 = 有断言失败
 * ════════════════════════════════════════════════════════════ */
int main(void)
{
    TEST_ASSERT_EQ(0, test_read_functions());
    TEST_ASSERT_EQ(0, test_write_functions());
    TEST_ASSERT_EQ(0, test_exceptions_and_silent_frames());
    TEST_ASSERT_EQ(0, test_too_short_requests());
    return 0;
}
