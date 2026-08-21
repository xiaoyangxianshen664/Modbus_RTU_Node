#ifndef MODBUS_CRC16_H
#define MODBUS_CRC16_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 计算 Modbus RTU 帧的 CRC-16 校验值。
 *
 * @param data   [输入] 待校验数据；length 为 0 时允许为 NULL。
 * @param length [输入] 待校验数据的字节数。
 * @return CRC-16 数值；发送时先发送低字节，再发送高字节。
 *
 * 示例：modbus_crc16(frame, sizeof(frame));
 */
uint16_t modbus_crc16(const uint8_t *data, size_t length);

#endif
