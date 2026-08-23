#include "./TIM6/TIM6.h"
#include "modbus_transport.h"   /* T3.5 定时器（TIM4 溢出时转发给传输层） */
#include "freertos_demo.h"      /* 完整帧事件通过任务通知唤醒 ModbusTask */

/* TIM6 句柄（非 static，供 it.c extern 引用） */
TIM_HandleTypeDef htim6;

/* 运行时间统计全局计数器，中断里每次 +1 */
uint32_t FreeRTOSRunTimeTicks;

/**
 * @brief  TIM6 基本定时器初始化 — 500ms 周期中断（备用）
 */
void TIM6_Init(void)
{
    TIM6_CLK_ENABLE();

    htim6.Instance         = TIM6;
    htim6.Init.Prescaler   = TIM6_PSC;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period      = TIM6_ARR;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    HAL_TIM_Base_Start_IT(&htim6);
}

/**
 * @brief  把 TIM6 重配为 100KHz 高精度时钟（用于 FreeRTOS 运行时间统计）
 * @note   PSC=89, ARR=9 → 90MHz/(89+1)/(9+1) = 100KHz = 10μs
 *         SysTick 是 1KHz，这个快 100 倍，满足 10~100 倍要求
 */
void ConfigureTimeForRunTimeStats(void)
{
    FreeRTOSRunTimeTicks = 0;               /* 计数器归零 */

    TIM6_CLK_ENABLE();                      /* 使能 TIM6 时钟 */
    htim6.Instance         = TIM6;
    htim6.Init.Prescaler   = RT_STATS_PSC;  /* 89 */
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period      = RT_STATS_ARR;  /* 9 */
    HAL_TIM_Base_Init(&htim6);

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 6, 0);  /* 抢占 6，低于关键中断 */
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    HAL_TIM_Base_Start_IT(&htim6);
}

/**
 * @brief  HAL 定时器"周期到达"回调（所有 TIM 共用）
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        FreeRTOSRunTimeTicks++;  /* 每次 10μs 中断 +1，全局运行时间计数器 */
    }
    else if (htim->Instance == TIM7)
    {
        HAL_IncTick();  /* TIM7: 1ms HAL 时基，替代 SysTick */
    }
    else if (htim->Instance == TIM4)
    {
        if (modbus_transport_on_timer() != 0U)  /* T3.5 刚到达，只通知一次 */
            modbus_task_notify_from_isr();      /* 用 FromISR API 唤醒 ModbusTask */
    }
}
