#ifndef __MODBUS_TRANSPORT_H
#define __MODBUS_TRANSPORT_H

#include "stm32f4xx_hal.h"

/* ════════════════════════════════════════════════════════════
 *  Modbus RTU 传输层 —— 用 T3.5 静默定时器判定"一帧收完"
 *
 *  原理：
 *    TIM4 每收 1 字节就清零重计，静默超过 T3.5（5ms）溢出 = 帧结束。
 *    若静默超过 T1.5 后、达到 T3.5 前又收到字节，则判定帧内间隔异常；
 *    若持续静默达到 T3.5，则正常判定一帧结束。
 * ════════════════════════════════════════════════════════════ */

void    modbus_transport_init(void);        /* 初始化 T3.5 定时器（TIM4） */
void    modbus_transport_on_byte(void);     /* 每收 1 字节调用：重置 T3.5 计时 */
uint8_t modbus_transport_on_timer(void);    /* TIM4 节拍调用；本次刚完成一帧时返回 1 */
uint8_t modbus_transport_frame_ready(void); /* 查询：一帧是否已收完 */
uint8_t modbus_transport_frame_valid(void); /* 查询：帧内是否没有超过 T1.5 的间隔 */
void    modbus_transport_frame_clear(void); /* 清"帧完成"标志 */

#endif
