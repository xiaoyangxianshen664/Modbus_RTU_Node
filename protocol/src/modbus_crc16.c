#include "modbus_crc16.h"

/**
 * 计算 Modbus CRC16 校验值
 * 参数：
 *   *data   - 待计算的数据数组首地址
 *   length -  数组的字节数
 * 返回：
 *   16 位 CRC 值（如 0xCDC5）
 *   注意：返回的是原始数值，实际发送时要"低字节在前"拆开发
 */
uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = UINT16_C(0xFFFF); 								  								// ① CRC 寄存器初始值设为 0xFFFF

    for (size_t index = 0; index < length; index++) 	 								// ② 逐个遍历输入数据的每个字节
    {
        crc ^= data[index]; 																					// 将当前字节与 CRC 低 8 位异或，送入寄存器，比如0x01，0xFFFF异或0x01得 0xFFFE

        for (uint8_t bit = 0; bit < 8U; bit++) 												// ③ 对当前字节的 8 个比特逐位处理（LSB 先行）
        {
            if ((crc & UINT16_C(0x0001)) != 0U) 											// 判断 CRC 寄存器最低位（LSB）是否为 1
            {
                crc = (uint16_t)((crc >> 1U) ^ UINT16_C(0xA001)); 		// 若为 1：右移 1 位后，再与多项式 0xA001 异或
            }
            else
            {
                crc >>= 1U; 																					// 若为 0：仅右移 1 位，不做异或
            }
        }
    }

    return crc; 																									    // ④ 返回最终计算出的 16 位 CRC 值（发送时低字节 C5 在前、高字节 CD 在后）
}

