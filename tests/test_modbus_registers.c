#include "modbus_registers.h"

#include <stdio.h>
#include <stdint.h>

/* ════════════════════════════════════════════════════════════
 * TEST_ASSERT_EQ — 比较期望值与实际值，不等则打印并返回失败
 * 不依赖标准 assert()，避免定义 NDEBUG 后测试失效。
 * ════════════════════════════════════════════════════════════ */
#define TEST_ASSERT_EQ(expected, actual)                                      \
    do {                                                                      \
        const unsigned expected_value = (unsigned)(expected);                 \
        const unsigned actual_value = (unsigned)(actual);                     \
        if (expected_value != actual_value) {                                 \
            printf("FAIL %s:%d: expected 0x%X, actual 0x%X\n",               \
                   __FILE__, __LINE__, expected_value, actual_value);          \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* ════════════════════════════════════════════════════════════
 * test_read_and_update — 测输入寄存器更新 + 两张表的读取
 *
 * 覆盖：
 *   更新输入寄存器 0 → 0x1234，再读回验证
 *   读保持寄存器 0/1 → 默认 1000 和 0
 * ════════════════════════════════════════════════════════════ */
static int test_read_and_update(void)
{
    modbus_registers_t registers;
    uint16_t values[2];

    modbus_registers_init(&registers);
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 更新输入寄存器 0
                   modbus_registers_update_input(&registers, 0U, UINT16_C(0x1234)));
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 读回输入寄存器 0
                   modbus_registers_read(&registers, MODBUS_REGISTER_INPUT, 0U, 1U, values));
    TEST_ASSERT_EQ(UINT16_C(0x1234), values[0]);                         // 验证值
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 读保持寄存器 0~1
                   modbus_registers_read(&registers, MODBUS_REGISTER_HOLDING, 0U, 2U, values));
    TEST_ASSERT_EQ(UINT16_C(1000), values[0]);                           // 采样周期默认 1000
    TEST_ASSERT_EQ(UINT16_C(0), values[1]);                              // 其余清零
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * test_write_is_atomic_and_validated — 测写保持寄存器的校验 + 原子性
 *
 * 覆盖：
 *   合法值 {200, 1} 写入成功
 *   非法值 {55, 2}（地址 3 的 SD 开关写 2）→ 整体拒绝，一个都不写
 *   读越界（起始 3 读 2 个）→ 返回 INVALID_ADDRESS
 * ════════════════════════════════════════════════════════════ */
static int test_write_is_atomic_and_validated(void)
{
    modbus_registers_t registers;
    const uint16_t valid_values[] = { UINT16_C(200), UINT16_C(1) };    // 合法：地址 1=200，地址 2=1
    const uint16_t invalid_values[] = { UINT16_C(55), UINT16_C(2) };   // 非法：地址 3 的 SD 开关不能写 2
    uint16_t values[2];

    modbus_registers_init(&registers);
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 写合法值，成功
                   modbus_registers_write_holding(&registers, 1U, 2U, valid_values));
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 读回验证
                   modbus_registers_read(&registers, MODBUS_REGISTER_HOLDING, 1U, 2U, values));
    TEST_ASSERT_EQ(UINT16_C(200), values[0]);
    TEST_ASSERT_EQ(UINT16_C(1), values[1]);

    TEST_ASSERT_EQ(MODBUS_REGISTER_INVALID_VALUE,                        // 写非法值，整体拒绝
                   modbus_registers_write_holding(&registers, 2U, 2U, invalid_values));
    TEST_ASSERT_EQ(MODBUS_REGISTER_OK,                                   // 读回，确认"一个都没写进去"
                   modbus_registers_read(&registers, MODBUS_REGISTER_HOLDING, 2U, 2U, values));
    TEST_ASSERT_EQ(UINT16_C(1), values[0]);                              // 地址 2 还是原来写的 1
    TEST_ASSERT_EQ(UINT16_C(0), values[1]);                              // 地址 3 还是 0（没被非法值污染）
    TEST_ASSERT_EQ(MODBUS_REGISTER_INVALID_ADDRESS,                      // 读越界：起始 3 读 2 个
                   modbus_registers_read(&registers, MODBUS_REGISTER_HOLDING, 3U, 2U, values));
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * main — 依次跑两个测试函数，任一失败即返回 1
 * 返回：0 = 全部通过，1 = 有断言失败
 * ════════════════════════════════════════════════════════════ */
int main(void)
{
    TEST_ASSERT_EQ(0, test_read_and_update());
    TEST_ASSERT_EQ(0, test_write_is_atomic_and_validated());
    return 0;
}
