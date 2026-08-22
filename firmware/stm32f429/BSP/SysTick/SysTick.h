#ifndef __SYSTICK_H
#define __SYSTICK_H

#include "stm32f4xx.h"

void SysTick_Init(void);                              /* 时钟配置 + SysTick 初始化     */

/* ── 轮询版延时：读 SysTick->VAL 硬件寄存器，不依赖中断
 *    调度器启动前/后、关中断期间均可用                    */
void delay_init(uint32_t sysclk);                     /* 初始化（传 SystemCoreClock）  */
void delay_us(uint32_t nus);                          /* 微秒级延时                     */
void delay_ms(uint32_t nms);                          /* 毫秒级延时                     */

#endif

/*
 * ========== SysTick 延时原理 ==========
 *
 * 调度器启动前：SysTick 配置为 10us 中断（给裸机 Delay_us 用）。
 * 调度器启动后：FreeRTOS 将 SysTick 重配为 1ms。
 * Delay_us/Delay_ms 依赖中断，调度器启动后精度退化，已废弃。
 *
 * 轮询版 delay_us/delay_ms 读 SysTick->VAL 硬件寄存器，
 * 计数器每个 HCLK 周期自动减 1，不依赖中断，全场景可用。
 *
 * 时间换算：1s = 1000ms = 1,000,000us，都是 1000 进制
 */
