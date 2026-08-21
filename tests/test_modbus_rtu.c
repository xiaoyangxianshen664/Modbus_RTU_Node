#include "modbus_rtu.h"
#include <stdio.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────
 * TEST_ASSERT_EQ
 * 功能：比较期望值与实际值是否相等。
 *       不相等时打印测试失败位置、期望值和实际值，并返回失败。
 * ───────────────────────────────────────────────────────────── */
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

/* ─────────────────────────────────────────────────────────────
 * main
 * 功能：测试 modbus_get_u16 和 modbus_put_u16 的正确性。
 *       覆盖边界值（0x0000, 0xFFFF）和典型值（0x00FF, 0x0100, 0x1234）。
 * 参数：无
 * 返回：0 = 全部通过，1 = 有断言失败
 * ───────────────────────────────────────────────────────────── */
int main(void)
{
    uint8_t bytes[2];

    /* ── 测试 modbus_get_u16：从字节数组解析 16 位值（大端序）── */
    bytes[0] = UINT8_C(0x00);
    bytes[1] = UINT8_C(0x00);
    TEST_ASSERT_EQ(UINT16_C(0x0000), modbus_get_u16(bytes)); // 全零 → 0x0000

    bytes[0] = UINT8_C(0x00);
    bytes[1] = UINT8_C(0xFF);
    TEST_ASSERT_EQ(UINT16_C(0x00FF), modbus_get_u16(bytes)); // 仅低字节非零 → 0x00FF

    bytes[0] = UINT8_C(0x01);
    bytes[1] = UINT8_C(0x00);
    TEST_ASSERT_EQ(UINT16_C(0x0100), modbus_get_u16(bytes)); // 仅高字节非零 → 0x0100

    bytes[0] = UINT8_C(0x12);
    bytes[1] = UINT8_C(0x34);
    TEST_ASSERT_EQ(UINT16_C(0x1234), modbus_get_u16(bytes)); // 典型值 → 0x1234

    /* ── 测试 modbus_put_u16：将 16 位值写入字节数组（大端序）── */
    modbus_put_u16(bytes, UINT16_C(0x0000));
    TEST_ASSERT_EQ(UINT8_C(0x00), bytes[0]); // 0x0000 → 高字节 0x00
    TEST_ASSERT_EQ(UINT8_C(0x00), bytes[1]); // 0x0000 → 低字节 0x00

    modbus_put_u16(bytes, UINT16_C(0x00FF));
    TEST_ASSERT_EQ(UINT8_C(0x00), bytes[0]); // 0x00FF → 高字节 0x00
    TEST_ASSERT_EQ(UINT8_C(0xFF), bytes[1]); // 0x00FF → 低字节 0xFF

    modbus_put_u16(bytes, UINT16_C(0x0100));
    TEST_ASSERT_EQ(UINT8_C(0x01), bytes[0]); // 0x0100 → 高字节 0x01
    TEST_ASSERT_EQ(UINT8_C(0x00), bytes[1]); // 0x0100 → 低字节 0x00

    modbus_put_u16(bytes, UINT16_C(0x1234));
    TEST_ASSERT_EQ(UINT8_C(0x12), bytes[0]); // 0x1234 → 高字节 0x12
    TEST_ASSERT_EQ(UINT8_C(0x34), bytes[1]); // 0x1234 → 低字节 0x34

    modbus_put_u16(bytes, UINT16_C(0xFFFF));
    TEST_ASSERT_EQ(UINT8_C(0xFF), bytes[0]); // 全 1 → 高字节 0xFF
    TEST_ASSERT_EQ(UINT8_C(0xFF), bytes[1]); // 全 1 → 低字节 0xFF

    return 0; // 所有断言通过，返回 0 表示测试成功
}