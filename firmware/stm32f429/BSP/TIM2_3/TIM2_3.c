#include "./TIM2_3/TIM2_3.h"
#include <stdio.h> 

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

void TIM2_3_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* TIM2：优先级 4（线上，BASEPRI 拦不住） */
    htim2.Instance         = TIM2;
    htim2.Init.Prescaler   = TIM2_3_PSC;
    htim2.Init.Period      = TIM2_3_ARR;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    HAL_TIM_Base_Init(&htim2);
    HAL_NVIC_SetPriority(TIM2_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2);

    /* TIM3：优先级 6（线下，BASEPRI 拦住） */
    htim3.Instance         = TIM3;
    htim3.Init.Prescaler   = TIM2_3_PSC;
    htim3.Init.Period      = TIM2_3_ARR;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    HAL_TIM_Base_Init(&htim3);
    HAL_NVIC_SetPriority(TIM3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Start_IT(&htim3);
}

/* ── 注意：HAL_TIM_PeriodElapsedCallback 已移到 App/modbus_transport.c（处理 TIM4 的 T3.5）。
 *  TIM2/TIM3 的 BASEPRI 分界线实验不再使用，这里不再重复定义回调，避免链接错误。 ── */
