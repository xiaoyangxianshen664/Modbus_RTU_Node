#include "modbus_crc16.h" // Modbus CRC16 函数声明
#include <stdio.h>        // printf 用
#include <stdint.h>       // uint8_t / uint16_t / UINT8_C / UINT16_C 等类型定义

/* ─────────────────────────────────────────────────────────────
 * TEST_ASSERT_EQ
 * 功能：比较期望值与实际值是否相等。
 *       不相等时打印测试失败位置、期望值和实际值，并返回失败。
 * 参数：
 *   expected - 期望结果
 *   actual   - 实际结果
 * 返回：无
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
 * 功能：测试 Modbus CRC16 的计算结果以及 CRC 的高低字节。
 *
 * 测试向量：
 *   01 03 00 00 00 0A
 *
 * 期望 CRC：
 *   0xCDC5
 *
 * Modbus RTU 发送时 CRC 低字节在前：
 *   C5 CD
 * ───────────────────────────────────────────────────────────── */

int main(void)
{
    static const uint8_t request[] = {
        // 测试数据：Modbus 读保持寄存器请求帧
        UINT8_C(0x01), // 从机地址 0x01
        UINT8_C(0x03), // 功能码 0x03（读保持寄存器）
        UINT8_C(0x00), // 起始地址高字节 0x00
        UINT8_C(0x00), // 起始地址低字节 0x00
        UINT8_C(0x00), // 寄存器数量高字节 0x00
        UINT8_C(0x0A), // 寄存器数量低字节 0x0A（即 10 个）
    };

    const uint16_t crc = modbus_crc16(request, sizeof(request)); // 调用 CRC16 计算，传入数据数组和长度

    TEST_ASSERT_EQ(UINT16_C(0xCDC5), crc);                            // 断言：CRC 整体值应为 0xCDC5
    TEST_ASSERT_EQ(UINT8_C(0xC5), (uint8_t)(crc & UINT16_C(0x00FF))); // 断言：低字节应为 0xC5（发送时先发这个）
    TEST_ASSERT_EQ(UINT8_C(0xCD), (uint8_t)(crc >> 8U));              // 断言：高字节应为 0xCD（发送时后发这个）

    return 0; // 全部断言通过，返回 0 表示测试成功
}