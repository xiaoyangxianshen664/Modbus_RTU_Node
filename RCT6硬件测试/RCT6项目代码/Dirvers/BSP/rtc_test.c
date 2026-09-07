#include "rtc_test.h"

RTC_HandleTypeDef g_rtc_test;

static volatile uint8_t g_rtc_alarm_event;

/**
 * @brief  配置LSE作为RTC时钟并启动F103备份域RTC测试
 * @param  backup_retained [输出] 1表示检测到上次写入的备份标志，0表示首次初始化
 * @param  alarm_time [输出] 返回本次设置的闹钟时间
 * @return HAL_OK表示RTC和30秒闹钟均配置成功，其他值表示对应HAL步骤失败
 * @note   F103 RTC保存的是连续秒计数，不是F429那种完整硬件日历；VBAT存在时可在主电源关闭后继续走时。
 * @example result = RTC_Test_Init(&retained, &alarm_time);
 */
HAL_StatusTypeDef RTC_Test_Init(uint8_t *backup_retained,
                                RTC_TimeTypeDef *alarm_time)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    RTC_TimeTypeDef current_time = {0};
    RTC_TimeTypeDef initial_time = {0};
    RTC_AlarmTypeDef alarm = {0};
    HAL_StatusTypeDef result;
    uint32_t alarm_seconds;

    if ((backup_retained == NULL) || (alarm_time == NULL))
    {
        return HAL_ERROR;
    }

    __HAL_RCC_PWR_CLK_ENABLE();                                      // 备份域写保护由PWR模块控制
    __HAL_RCC_BKP_CLK_ENABLE();                                      // 使能F103独立的备份寄存器接口
    HAL_PWR_EnableBkUpAccess();                                      // 允许修改RTC、LSE和BKP寄存器

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    oscillator.LSEState = RCC_LSE_ON;                                // PC14、PC15连接32.768kHz晶振，无需配置GPIO
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    result = HAL_RCC_OscConfig(&oscillator);
    if (result != HAL_OK)
    {
        return result;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    peripheral_clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    result = HAL_RCCEx_PeriphCLKConfig(&peripheral_clock);            // 仅在时钟源发生变化时HAL才复位备份域
    if (result != HAL_OK)
    {
        return result;
    }

    __HAL_RCC_RTC_ENABLE();

    g_rtc_test.Instance = RTC;
    g_rtc_test.Init.AsynchPrediv = 32767U;                            // 32768Hz/(32767+1)=1Hz
    g_rtc_test.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
    result = HAL_RTC_Init(&g_rtc_test);
    if (result != HAL_OK)
    {
        return result;
    }

    *backup_retained = (HAL_RTCEx_BKUPRead(&g_rtc_test,
                                            RTC_TEST_BACKUP_REGISTER) ==
                        RTC_TEST_BACKUP_MARK) ? 1U : 0U;

    if (*backup_retained == 0U)
    {
        initial_time.Hours = 12U;
        initial_time.Minutes = 0U;
        initial_time.Seconds = 0U;
        result = HAL_RTC_SetTime(&g_rtc_test, &initial_time, RTC_FORMAT_BIN);
        if (result != HAL_OK)
        {
            return result;
        }

        HAL_RTCEx_BKUPWrite(&g_rtc_test,
                            RTC_TEST_BACKUP_REGISTER,
                            RTC_TEST_BACKUP_MARK);                    // VBAT正常时该标志和RTC计数都不会因断主电源丢失
    }

    result = HAL_RTC_GetTime(&g_rtc_test, &current_time, RTC_FORMAT_BIN);
    if (result != HAL_OK)
    {
        return result;
    }

    alarm_seconds = (uint32_t)current_time.Hours * 3600U +
                    (uint32_t)current_time.Minutes * 60U +
                    (uint32_t)current_time.Seconds + RTC_TEST_ALARM_DELAY_S;
    alarm_seconds %= 24U * 3600U;

    alarm_time->Hours = (uint8_t)(alarm_seconds / 3600U);
    alarm_time->Minutes = (uint8_t)((alarm_seconds % 3600U) / 60U);
    alarm_time->Seconds = (uint8_t)(alarm_seconds % 60U);
    alarm.Alarm = RTC_ALARM_A;
    alarm.AlarmTime = *alarm_time;

    g_rtc_alarm_event = 0U;
    result = HAL_RTC_SetAlarm_IT(&g_rtc_test, &alarm, RTC_FORMAT_BIN);
    if (result != HAL_OK)
    {
        return result;
    }

    HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
    return HAL_OK;
}

/**
 * @brief  读取F103 RTC当前时分秒
 * @param  time [输出] 接收当前时间的结构体
 * @return HAL_OK表示读取成功，HAL_ERROR表示参数为空，其他值来自HAL RTC驱动
 * @note   超过24小时后HAL会折算为新一天的时分秒，当前测试不显示日期。
 * @example RTC_Test_GetTime(&time);
 */
HAL_StatusTypeDef RTC_Test_GetTime(RTC_TimeTypeDef *time)
{
    if (time == NULL)
    {
        return HAL_ERROR;
    }

    return HAL_RTC_GetTime(&g_rtc_test, time, RTC_FORMAT_BIN);
}

/**
 * @brief  读取并清除一次RTC闹钟事件
 * @param  无
 * @return 1表示闹钟刚触发，0表示没有待处理事件
 * @note   中断只置位标志，串口打印和LED操作留在主循环执行。
 * @example if (RTC_Test_TakeAlarmEvent() != 0U) { LED_Toggle(BSP_LED_GREEN); }
 */
uint8_t RTC_Test_TakeAlarmEvent(void)
{
    uint8_t event = g_rtc_alarm_event;

    g_rtc_alarm_event = 0U;
    return event;
}

/**
 * @brief  将RTC闹钟中断交给HAL处理
 * @param  无
 * @return 无
 * @note   由RTC_Alarm_IRQHandler调用，HAL随后进入HAL_RTC_AlarmAEventCallback。
 * @example void RTC_Alarm_IRQHandler(void) { RTC_Test_AlarmIRQHandler(); }
 */
void RTC_Test_AlarmIRQHandler(void)
{
    HAL_RTC_AlarmIRQHandler(&g_rtc_test);
}

/**
 * @brief  HAL的RTC闹钟A事件回调
 * @param  hrtc [输入] 触发回调的RTC句柄
 * @return 无
 * @note   只记录事件，避免在中断上下文中打印或延时。
 * @example 由HAL_RTC_AlarmIRQHandler自动调用。
 */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    if (hrtc == &g_rtc_test)
    {
        g_rtc_alarm_event = 1U;
    }
}
