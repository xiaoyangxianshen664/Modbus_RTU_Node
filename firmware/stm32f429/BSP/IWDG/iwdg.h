#ifndef __IWDG_H
#define __IWDG_H

#include "stm32f4xx.h"

/* ════════════════════════════════════════════════════════════
 * 函数声明
 * ════════════════════════════════════════════════════════════ */
void IWDG_Config(uint8_t prv, uint16_t rlv);                         // 配独立看门狗超时时间
void IWDG_Feed(void);                                                 // 喂狗（重置计数器）

#endif /* __IWDG_H */
