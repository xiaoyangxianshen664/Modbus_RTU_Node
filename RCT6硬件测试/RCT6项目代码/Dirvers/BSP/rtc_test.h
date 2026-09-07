#ifndef __RTC_TEST_H
#define __RTC_TEST_H

#include "stm32f1xx_hal.h"

#define RTC_TEST_BACKUP_REGISTER RTC_BKP_DR1
#define RTC_TEST_BACKUP_MARK      0x32F2U
#define RTC_TEST_ALARM_DELAY_S    30U

extern RTC_HandleTypeDef g_rtc_test;

HAL_StatusTypeDef RTC_Test_Init(uint8_t *backup_retained,
                                RTC_TimeTypeDef *alarm_time);
HAL_StatusTypeDef RTC_Test_GetTime(RTC_TimeTypeDef *time);
uint8_t RTC_Test_TakeAlarmEvent(void);
void RTC_Test_AlarmIRQHandler(void);

#endif /* __RTC_TEST_H */
