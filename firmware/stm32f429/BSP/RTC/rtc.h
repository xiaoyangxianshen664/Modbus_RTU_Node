#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx.h"

extern RTC_HandleTypeDef Rtc_Handle;                                 // RTC 句柄

/* ── 时钟源：LSE 外部 32.768KHz 晶振 ── */
#define RTC_CLOCK_SOURCE_LSE

/* ── 预分频：LSE/(128×256) = 32768/32768 = 1Hz ── */
#define ASYNCHPREDIV  0x7F                                            // 异步预分频 127+1=128
#define SYNCHPREDIV   0xFF                                            // 同步预分频 255+1=256

/* ── 首次初始化的默认时间 ── */
#define HOURS     12
#define MINUTES   0
#define SECONDS   0
#define WEEKDAY   1
#define DATE      1
#define MONTH     1
#define YEAR      26                                                // 20YY，YY=26 → 2026

/* ── 闹钟时间 ── */
#define ALARM_HOURS   12
#define ALARM_MINUTES 0
#define ALARM_SECONDS 30                                              // 12:00:30 闹钟响

/* ── 备份寄存器 ── */
#define RTC_BKP_DRX      RTC_BKP_DR0                                 // 备份寄存器 0
#define RTC_BKP_DATA      0x32F2                                     // 标记 RTC 已初始化

/* ── 函数声明 ── */
void RTC_CLK_Config(void);                                           // 初始化 RTC 时钟（LSE + 预分频）
void RTC_TimeAndDate_Set(void);                                      // 设默认时间 + 写备份标志
void RTC_TimeAndDate_Show(void);                                     // while(1) 循环显示时间
void RTC_AlarmSet(void);                                             // 设闹钟 + 开中断

#endif /* __RTC_H */
