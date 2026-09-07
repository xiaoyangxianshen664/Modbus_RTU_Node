#ifndef __LED_H
#define __LED_H

#include "stm32f1xx_hal.h"

/** ∞Â‘ÿ LED ±‡∫≈°£ */
typedef enum
{
    BSP_LED_RED = 0,
    BSP_LED_GREEN,
    BSP_LED_COUNT
} BSP_LED_TypeDef;

void LED_Init(void);
void LED_On(BSP_LED_TypeDef led);
void LED_Off(BSP_LED_TypeDef led);
void LED_Toggle(BSP_LED_TypeDef led);

#endif /* __LED_H */
