#include "modbus_registers.h"

#include <string.h>

/* ════════════════════════════════════════════════════════════
 * table_count — 返回指定寄存器表的容量
 *
 * 原型：static uint16_t table_count(modbus_register_table_t table)
 *       table — [输入] 输入寄存器或保持寄存器表
 * 返值：该表的寄存器数量；表类型非法返回 0
 *
 * 调用示例：
 *   uint16_t total = table_count(MODBUS_REGISTER_INPUT);
 * ════════════════════════════════════════════════════════════ */

static uint16_t table_count(modbus_register_table_t table)
{
    if (table == MODBUS_REGISTER_INPUT)
    {
        return MODBUS_INPUT_REGISTER_COUNT; // 返回输入寄存器数量：12 个
    }
    if (table == MODBUS_REGISTER_HOLDING)
    {
        return MODBUS_HOLDING_REGISTER_COUNT; // 返回保持寄存器数量：4 个
    }
    return 0U; // 非法表类型，返回 0
}

/* ════════════════════════════════════════════════════════════
 * holding_value_is_valid — 判断写往某个保持寄存器的值是否合法
 *
 * 原型：static int holding_value_is_valid(uint16_t address, uint16_t value)
 *       address — [输入] 保持寄存器地址
 *       value   — [输入] 待写入的值
 * 返值：1 = 合法，0 = 非法
 *
 * 原理（对应阶段 0 的寄存器表约束）：
 *   0x0000 采样周期：不能为 0（否则采集任务没法定时）
 *   0x0003 SD 日志开关：只能是 0 或 1（布尔开关）
 *   其余地址：暂不限制
 *
 * 调用示例：
 *   if (!holding_value_is_valid(addr, val)) { ... }
 * ════════════════════════════════════════════════════════════ */

static int holding_value_is_valid(uint16_t address, uint16_t value)
{
    if (address == 0U)
    {
        return value != 0U; // 采样周期：必须 > 0
    }
    if (address == 3U)
    {
        return value <= 1U; // SD 日志开关：只能是 0 或 1
    }
    return 1; // 其余地址无限制，直接返回合法
}

/* ════════════════════════════════════════════════════════════
 * modbus_registers_init — 初始化寄存器模型
 *
 * 原型：void modbus_registers_init(modbus_registers_t *registers)
 *       registers — [输出] 待初始化的寄存器模型
 * 返值：无
 *
 * 原理：
 *   全部清零，再把保持寄存器 0x0000（采样周期）设成默认 1000ms。
 *
 * 调用示例：
 *   modbus_registers_t registers;
 *   modbus_registers_init(&registers);
 * ════════════════════════════════════════════════════════════ */
void modbus_registers_init(modbus_registers_t *registers)
{
    memset(registers, 0, sizeof(*registers)); // 将整个寄存器模型清零
    registers->holding[0] = UINT16_C(1000);   // 保持寄存器 0x0000 默认采样周期 1000ms
}

/* ════════════════════════════════════════════════════════════
 * modbus_registers_update_input — 更新一个输入寄存器的采集值
 *
 * 原型：modbus_register_status_t modbus_registers_update_input(
 *         modbus_registers_t *registers, uint16_t address, uint16_t value)
 *       registers — [输入输出] 寄存器模型
 *       address   — [输入] 输入寄存器地址
 *       value     — [输入] 新的采集值
 * 返值：OK 或 INVALID_ADDRESS（地址越界）
 *
 * 用途：阶段 4 采集任务拿到 ADC 值后，通过它把值写进输入寄存器。
 *
 * 调用示例：
 *   modbus_registers_update_input(&registers, 0U, adc_raw);
 * ════════════════════════════════════════════════════════════ */
modbus_register_status_t modbus_registers_update_input(
    modbus_registers_t *registers,
    uint16_t address,
    uint16_t value)
{
    if (address >= MODBUS_INPUT_REGISTER_COUNT)
    {
        return MODBUS_REGISTER_INVALID_ADDRESS; // 地址越界，返回错误
    }

    registers->input[address] = value; // 将采集值写入对应输入寄存器
    return MODBUS_REGISTER_OK;         // 写入成功
}

/* ════════════════════════════════════════════════════════════
 * modbus_registers_read — 读取连续的输入或保持寄存器
 *
 * 原型：modbus_register_status_t modbus_registers_read(
 *         const modbus_registers_t *registers, modbus_register_table_t table,
 *         uint16_t address, uint16_t count, uint16_t *values)
 *       registers — [输入] 寄存器模型（只读）
 *       table     — [输入] 读哪张表
 *       address   — [输入] 起始地址
 *       count     — [输入] 连续读几个
 *       values    — [输出] 接收结果的数组
 * 返值：OK / INVALID_TABLE（表类型非法）/ INVALID_ADDRESS（越界或数量 0）
 *
 * 原理：
 *   先校验范围（0 < count 且 address+count 不超出表），再逐格拷贝。
 *   count > total - address 用减法算，避免 address+count 溢出。
 *
 * 调用示例：
 *   modbus_registers_read(&registers, MODBUS_REGISTER_INPUT, 0U, 2U, values);
 * ════════════════════════════════════════════════════════════ */
modbus_register_status_t modbus_registers_read(
    const modbus_registers_t *registers,
    modbus_register_table_t table,
    uint16_t address,
    uint16_t count,
    uint16_t *values)
{
    const uint16_t total = table_count(table); // 获取目标表的寄存器总数量
    const uint16_t *source;                    // 指向输入表或保持表数据源的指针

    if (total == 0U)
    {
        return MODBUS_REGISTER_INVALID_TABLE; // 表类型非法，返回错误
    }
    if (count == 0U || address >= total || count > (uint16_t)(total - address))
    {
        return MODBUS_REGISTER_INVALID_ADDRESS; // 数量为零 / 起始越界 / 超出表末尾
    }

    source = table == MODBUS_REGISTER_INPUT ? registers->input : registers->holding; // 根据表类型选择数据源
    for (uint16_t index = 0U; index < count; ++index)
    {
        values[index] = source[address + index]; // 逐格拷贝寄存器值到输出数组
    }
    return MODBUS_REGISTER_OK; // 读取成功
}

/* ════════════════════════════════════════════════════════════
 * modbus_registers_write_holding — 原子写入连续的保持寄存器
 *
 * 原型：modbus_register_status_t modbus_registers_write_holding(
 *         modbus_registers_t *registers, uint16_t address, uint16_t count,
 *         const uint16_t *values)
 *       registers — [输入输出] 寄存器模型
 *       address   — [输入] 起始地址
 *       count     — [输入] 写几个
 *       values    — [输入] 待写入的值数组
 * 返值：OK / INVALID_ADDRESS / INVALID_VALUE
 *
 * 原理（关键：原子性）：
 *   分两趟：第一趟【只校验】所有地址和值都合法；
 *   全部通过后，第二趟才真正写入。
 *   这样保证"要么全写、要么一个都不写"，不会写一半发现后面的值非法。
 *
 * 调用示例：
 *   modbus_registers_write_holding(&registers, 1U, 2U, values);
 * ════════════════════════════════════════════════════════════ */
modbus_register_status_t modbus_registers_write_holding(
    modbus_registers_t *registers,
    uint16_t address,
    uint16_t count,
    const uint16_t *values)
{
    if (count == 0U || address >= MODBUS_HOLDING_REGISTER_COUNT ||
        count > (uint16_t)(MODBUS_HOLDING_REGISTER_COUNT - address))
    {
        return MODBUS_REGISTER_INVALID_ADDRESS; // 检查错误：是否数量为零 / 是否起始越界 / 是否超出表末尾
    }

    for (uint16_t index = 0U; index < count; ++index)
    { // 第一趟：逐地址校验值的合法性
        if (!holding_value_is_valid(address + index, values[index]))
        {
            return MODBUS_REGISTER_INVALID_VALUE; // 任一值非法则整体拒绝，保证原子性
        }
    }

    for (uint16_t index = 0U; index < count; ++index)
    {                                                        // 第二趟：全部校验通过，执行真正写入
        registers->holding[address + index] = values[index]; // 将值写入对应保持寄存器
    }
    return MODBUS_REGISTER_OK; // 全部写入成功
}