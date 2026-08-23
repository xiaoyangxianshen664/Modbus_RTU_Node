#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stddef.h>
#include <stdint.h>

#include "modbus_registers.h"

/* ════════════════════════════════════════════════════════════
 * Modbus RTU 全局常量定义
 * ════════════════════════════════════════════════════════════ */

#define MODBUS_DEFAULT_SLAVE_ADDRESS UINT8_C(0x01) // 默认从站地址（单播地址）
#define MODBUS_BROADCAST_ADDRESS UINT8_C(0x00)     // 广播地址：所有从站接收但不回复
#define MODBUS_MAX_ADU_SIZE 256U                   // ADU 最大长度（地址+PDU+CRC）
#define MODBUS_MAX_READ_REGISTERS 125U             // 单次读寄存器最大数量（Modbus 规范）
#define MODBUS_MAX_WRITE_REGISTERS 123U            // 单次写寄存器最大数量（Modbus 规范）

/* ════════════════════════════════════════════════════════════
 * Modbus RTU 响应状态枚举
 * ════════════════════════════════════════════════════════════ */
typedef enum
{
    MODBUS_RTU_RESPONSE_READY = 0,        // 响应已准备好，可以发送
    MODBUS_RTU_NO_RESPONSE,               // 不回复（坏帧 / 非本机 / 广播读）
    MODBUS_RTU_INVALID_ARGUMENT,          // 参数非法（空指针等）
    MODBUS_RTU_RESPONSE_BUFFER_TOO_SMALL, // 响应缓冲区不足
} modbus_rtu_status_t;

uint16_t modbus_get_u16(const uint8_t *bytes);       // 按大端序拼接 16 位值（高字节 <<8 | 低字节）
void modbus_put_u16(uint8_t *bytes, uint16_t value); // 将 16 位值拆分为大端字节序列
modbus_rtu_status_t modbus_rtu_process_request(      // 校验请求并生成 Modbus RTU 响应

    const uint8_t *request,                          // 输入：原始请求 ADU（地址+PDU+CRC）
    size_t request_length,                           // 输入：请求 ADU 字节数
    modbus_registers_t *registers,                   // 输入/输出：寄存器映射表
    uint8_t *response,                               // 输出：响应 ADU 缓冲区
    size_t response_capacity,                        // 输入：响应缓冲区容量
    size_t *response_length                          // 输出：实际响应长度（无响应时为 0）
);

#endif
