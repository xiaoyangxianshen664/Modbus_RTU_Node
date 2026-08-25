#ifndef MODBUS_REGISTERS_H
#define MODBUS_REGISTERS_H

#include <stdint.h>

#define MODBUS_INPUT_REGISTER_COUNT UINT16_C(12)  // 输入寄存器数量：12 个（只读，存放 ADC/状态等采集值）
#define MODBUS_HOLDING_REGISTER_COUNT UINT16_C(4) // 保持寄存器数量：4 个（可读写，存放配置参数）

/* ════════════════════════════════════════════════════════════
 * 寄存器表类型枚举
 * ════════════════════════════════════════════════════════════ */
typedef enum
{
    MODBUS_REGISTER_INPUT = 0,   // 输入寄存器表（对应功能码 0x04 读输入寄存器）
    MODBUS_REGISTER_HOLDING = 1, // 保持寄存器表（对应功能码 0x03/0x06/0x10）
} modbus_register_table_t;

/* ════════════════════════════════════════════════════════════
 * 寄存器操作状态枚举
 * ════════════════════════════════════════════════════════════ */
typedef enum
{
    MODBUS_REGISTER_OK = 0,          // 操作成功
    MODBUS_REGISTER_INVALID_ADDRESS, // 地址越界或数量非法
    MODBUS_REGISTER_INVALID_VALUE,   // 写入值不符合业务约束（如采样周期为 0）
    MODBUS_REGISTER_INVALID_TABLE,   // 表类型非法（非 INPUT 也非 HOLDING）
} modbus_register_status_t;

/* ════════════════════════════════════════════════════════════
 * 寄存器模型结构体
 * ════════════════════════════════════════════════════════════ */
typedef struct
{
    uint16_t input[MODBUS_INPUT_REGISTER_COUNT];     // 输入寄存器数组（只读，存放采集值）
    uint16_t holding[MODBUS_HOLDING_REGISTER_COUNT]; // 保持寄存器数组（可读写，存放配置参数）
} modbus_registers_t;

void modbus_registers_init(modbus_registers_t *registers); // 清零全部寄存器，并设置采样周期及温度报警默认值

modbus_register_status_t modbus_registers_update_input( // 更新一个输入寄存器的采集值
    modbus_registers_t *registers,                      // 输入/输出：寄存器模型
    uint16_t address,                                   // 输入：输入寄存器地址（0 ~ COUNT-1）
    uint16_t value                                      // 输入：待写入的 16 位采集值
);

modbus_register_status_t modbus_registers_read( // 读取连续的输入寄存器或保持寄存器。
    const modbus_registers_t *registers,        // 输入：寄存器模型（只读）
    modbus_register_table_t table,              // 输入：选择输入表或保持表
    uint16_t address,                           // 输入：起始寄存器地址
    uint16_t count,                             // 输入：连续读取数量
    uint16_t *values                            // 输出：接收读取结果的数组
);

modbus_register_status_t modbus_registers_write_holding( // 原子写入连续的保持寄存器。
    modbus_registers_t *registers,                       // 输入/输出：寄存器模型
    uint16_t address,                                    // 输入：起始地址
    uint16_t count,                                      // 输入：连续写入数量
    const uint16_t *values                               // 输入：待写入的值数组（先全部校验再原子写入）
);

#endif
