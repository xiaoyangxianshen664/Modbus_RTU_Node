#ifndef __KEY_H
#define __KEY_H

#include "stm32f1xx_hal.h"

/** 板载软件按键编号；硬件复位键不由该 BSP 读取。 */
typedef enum
{
    BSP_KEY_1 = 0,
    BSP_KEY_2,
    BSP_KEY_COUNT
} BSP_KEY_TypeDef;

void KEY_Init(void);
uint8_t KEY_IsPressed(BSP_KEY_TypeDef key);

#endif /* __KEY_H */
