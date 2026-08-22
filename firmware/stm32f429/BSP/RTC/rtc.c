#include <stdio.h>
#include "./RTC/rtc.h"
#include "./LED/LED.h"

RTC_HandleTypeDef Rtc_Handle;                                        // RTC 句柄
volatile uint8_t AlarmFlag = 0;                                      // 闹钟标志：1=响了

/* ════════════════════════════════════════════════════════════
 * RTC_CLK_Config — 初始化 RTC 时钟（LSE 32.768KHz）
 *
 * 原型：void RTC_CLK_Config(void)
 *       无参数，无返回值
 *
 * 原理：LSE(32768Hz) / (AsynchPrediv+1) / (SynchPrediv+1)
 *       = 32768 / 128 / 256 = 1Hz → RTC 每秒跳一次
 *
 * 调用示例：
 *   RTC_CLK_Config();  // main 里第一个调
 * ════════════════════════════════════════════════════════════ */
void RTC_CLK_Config(void)
{
    RCC_OscInitTypeDef       osc = {0};                               // 振荡器配置
    RCC_PeriphCLKInitTypeDef clk = {0};                               // 外设时钟配置

    Rtc_Handle.Instance = RTC;                                         // RTC 外设

    __HAL_RCC_PWR_CLK_ENABLE();                                        // 开 PWR 时钟（RTC 在备份域，需要 PWR）
    HAL_PWR_EnableBkUpAccess();                                        // 允许访问备份域（RTC + 备份 SRAM.BKP）

    /* ① 开 LSE 外部 32.768KHz 晶振 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;                      // 只开 LSE
    osc.PLL.PLLState   = RCC_PLL_NONE;                                 // 不用 PLL，会直接跳过关于 PLL 的所有检查和操作，保持PLL原本寄存器的值
    osc.LSEState       = RCC_LSE_ON;                                   // LSE = ON
    HAL_RCC_OscConfig(&osc);

    /* ② 选 LSE 作为 RTC 时钟源 */
    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;                    	// 给配 RTC 时钟，LSE 作为 RTC 时钟源
    clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;                  // 时钟源 = LSE
    HAL_RCCEx_PeriphCLKConfig(&clk);

    /* ③ 使能 RTC + 等同步 */
    __HAL_RCC_RTC_ENABLE();
    HAL_RTC_WaitForSynchro(&Rtc_Handle);

    /* ④ 配预分频：LSE/(128×256) = 1Hz */
    Rtc_Handle.Init.AsynchPrediv = ASYNCHPREDIV;                      // LSE 32768khz / 128 = 256Hz
    Rtc_Handle.Init.SynchPrediv  = SYNCHPREDIV;                       // 256Hz / 256 = 1Hz
    Rtc_Handle.Init.HourFormat   = RTC_HOURFORMAT_24;                 // 24 小时制（另一个可选是12小时制）
    HAL_RTC_Init(&Rtc_Handle);
}

/* ════════════════════════════════════════════════════════════
 * RTC_TimeAndDate_Set — 设默认时间 + 写备份标志
 *
 * 原型：void RTC_TimeAndDate_Set(void)
 *       无参数，无返回值
 *
 * 原理：首次上电调用，设定初始时间，并在备份寄存器写 0x32F2 标记"已初始化"。
 *       之后上电读到该标记跳过初始化，RTC 一直在后台跑。
 *
 * 调用示例：
 *   RTC_TimeAndDate_Set();  // 首次初始化或需要重新校准时间时调
 * ════════════════════════════════════════════════════════════ */
void RTC_TimeAndDate_Set(void)
{
    RTC_TimeTypeDef t = {0};                                          // 时间结构体
    RTC_DateTypeDef d = {0};                                          // 日期结构体

    /* 时间：取自宏定义 HOURS:MINUTES:SECONDS */
    t.TimeFormat = RTC_HOURFORMAT_24;                                  // 24 小时制
    t.Hours      = HOURS;                                              // 时（0~23），初始我们设置为12
    t.Minutes    = MINUTES;                                            // 分（0~59），初始我们设置为0
    t.Seconds    = SECONDS;                                            // 秒（0~59），初始我们设置为0
    HAL_RTC_SetTime(&Rtc_Handle, &t, RTC_FORMAT_BIN);                 // 写入 RTC，二进制格式（程序员最爱）

    /* 日期：取自宏定义 YEAR/MONTH/DATE/WEEKDAY */
    d.WeekDay = WEEKDAY;                                               // 星期（1~7），初始我们设置为1
    d.Date    = DATE;                                                  // 日（1~31） ，初始我们设置为1
    d.Month   = MONTH;                                                 // 月（1~12） ，初始我们设置为1
    d.Year    = YEAR;                                                  // 年（0~99，20YY），初始我们设置为26
    HAL_RTC_SetDate(&Rtc_Handle, &d, RTC_FORMAT_BIN);                 // 写入 RTC，二进制格式（程序员最爱）

    /* 写备份寄存器 = 标记已初始化 */
    HAL_RTCEx_BKUPWrite(&Rtc_Handle, RTC_BKP_DRX, RTC_BKP_DATA);     // 写 0x32F2，我们自己随便定的值， 到备份数据寄存器DR0
}


/* ════════════════════════════════════════════════════════════
 * RTC_TimeAndDate_Show — 循环显示时间（串口打印 + LED 闪闹钟）
 *
 * 原型：void RTC_TimeAndDate_Show(void)
 *       无参数，无返回值（死循环不退出）
 *
 * 原理：每秒读一次 RTC → 串口打印 → 检查 AlarmFlag（闹钟响则 LED 闪）
 *
 * 调用示例：
 *   RTC_TimeAndDate_Show();  // main 最后调，进入主循环
 * ════════════════════════════════════════════════════════════ */
void RTC_TimeAndDate_Show(void)
{
    RTC_TimeTypeDef t;																								// 时间结构体
    RTC_DateTypeDef d;																								// 日期结构体
    uint8_t last_sec = 0xFF;                                          // 上次打印的秒（防重复打印）
    uint8_t alarm_cnt = 0;                                            // 闹钟闪烁计数器

    while (1)
    {
        HAL_RTC_GetTime(&Rtc_Handle, &t, RTC_FORMAT_BIN);             // 读当前时间
        HAL_RTC_GetDate(&Rtc_Handle, &d, RTC_FORMAT_BIN);             // 读当前日期

        /* 每秒打印一次 */
        if (t.Seconds != last_sec)
        {
            last_sec = t.Seconds;

            printf("20%02d-%02d-%02d W%d  %02d:%02d:%02d\r\n",
                   d.Year, d.Month, d.Date, d.WeekDay,
                   t.Hours, t.Minutes, t.Seconds);
        }

        /* 闹钟响了：LED 闪烁 30 次 */
        if (AlarmFlag)
        {
            alarm_cnt++;
            if (alarm_cnt < 30)
            {
                LED_R(0);                                               // 红灯亮
                HAL_Delay(200);                                         // 200ms
                LED_R(1);                                               // 红灯灭
                HAL_Delay(200);
            }
            else
            {
                AlarmFlag = 0;                                          // 30 次到，停止
                alarm_cnt = 0;
            }
        }
    }
}

/* ════════════════════════════════════════════════════════════
 * RTC_AlarmSet — 设置闹钟 A + 开中断
 *
 * 原型：void RTC_AlarmSet(void)
 *       无参数，无返回值
 *
 * 原理：配闹钟时间 → HAL_RTC_SetAlarm_IT → 到点触发 RTC_Alarm_IRQn
 *
 * 调用示例：
 *   RTC_AlarmSet();  // 首次初始化时调一次
 * ════════════════════════════════════════════════════════════ */
void RTC_AlarmSet(void)
{
    RTC_AlarmTypeDef a = {0};                                         // 闹钟结构体

    a.Alarm                         = RTC_ALARM_A;                     // 闹钟 A
    a.AlarmTime.TimeFormat          = RTC_HOURFORMAT_24;               // 24 小时制
    a.AlarmTime.Hours               = ALARM_HOURS;                     // 闹钟时，初始我们设置为12
    a.AlarmTime.Minutes             = ALARM_MINUTES;                   // 闹钟分，初始我们设置为 0
    a.AlarmTime.Seconds             = ALARM_SECONDS;                   // 闹钟秒，初始我们设置为 30
    a.AlarmMask                     = RTC_ALARMMASK_DATEWEEKDAY;       // 屏蔽（忽略）日期和星期，只对比 秒+分+时
    a.AlarmDateWeekDaySel           = RTC_ALARMDATEWEEKDAYSEL_DATE;    // 按日期匹配，上面屏蔽了，填进去也只是占位
    a.AlarmDateWeekDay              = DATE;                            // 匹配日期=1 号，上面屏蔽了，填进去也只是占位

    HAL_RTC_SetAlarm_IT(&Rtc_Handle, &a, RTC_FORMAT_BIN);             // 设闹钟 + 开中断

    HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 0, 0);                       // 闹钟中断优先级最高
    HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);                                // 使能闹钟中断
}

/* ════════════════════════════════════════════════════════════
 * HAL_RTC_AlarmAEventCallback — 闹钟中断回调
 *
 * 闹钟到点 → RTC_Alarm_IRQn → 此回调 → AlarmFlag=1
 * main 循环检测到 AlarmFlag → LED 闪 30 次
 * ════════════════════════════════════════════════════════════ */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    AlarmFlag = 1;                                                     // 闹钟响了
}
