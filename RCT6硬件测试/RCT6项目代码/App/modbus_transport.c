#include "modbus_transport.h"
#include "FreeRTOS.h"
#include "task.h"

TIM_HandleTypeDef g_modbus_timer;

static volatile uint8_t g_frame_ready;
static volatile uint8_t g_frame_valid = 1U;
static volatile uint8_t g_silence_ms;
static volatile uint8_t g_has_data;

/**
 * @brief 初始化Modbus RTU静默时间定时器。
 * @note  F103的APB1为36MHz，但定时器时钟自动倍频为72MHz；TIM4每1ms中断一次。
 */
void modbus_transport_init(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();
    g_modbus_timer.Instance = TIM4;
    g_modbus_timer.Init.Prescaler = 72U - 1U;
    g_modbus_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_modbus_timer.Init.Period = 1000U - 1U;
    g_modbus_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_modbus_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    configASSERT(HAL_TIM_Base_Init(&g_modbus_timer) == HAL_OK);

    HAL_NVIC_SetPriority(TIM4_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
    configASSERT(HAL_TIM_Base_Start_IT(&g_modbus_timer) == HAL_OK);
}

/** 每收到一个字节就重新计算静默时间，并检查帧内T1.5超时。 */
void modbus_transport_on_byte(void)
{
    if ((g_silence_ms >= 2U) && (g_silence_ms < 5U))
    {
        g_frame_valid = 0U;
    }
    __HAL_TIM_SET_COUNTER(&g_modbus_timer, 0U);
    g_silence_ms = 0U;
    g_has_data = 1U;
}

uint8_t modbus_transport_frame_ready(void)
{
    return g_frame_ready;
}

uint8_t modbus_transport_frame_valid(void)
{
    return g_frame_valid;
}

/** 清除上一帧状态，使传输层准备接收下一帧。 */
void modbus_transport_frame_clear(void)
{
    g_frame_ready = 0U;
    g_frame_valid = 1U;
    g_silence_ms = 0U;
    g_has_data = 0U;
}

/**
 * @brief 在TIM4的1ms节拍中判断是否达到T3.5。
 * @return 1表示本次刚确认一帧结束，需要唤醒ModbusTask。
 */
uint8_t modbus_transport_on_timer(void)
{
    if ((g_has_data == 0U) || (g_frame_ready != 0U))
    {
        return 0U;
    }
    if (g_silence_ms < 255U)
    {
        g_silence_ms++;
    }
    if (g_silence_ms >= 5U)
    {
        g_frame_ready = 1U;
        return 1U;
    }
    return 0U;
}
