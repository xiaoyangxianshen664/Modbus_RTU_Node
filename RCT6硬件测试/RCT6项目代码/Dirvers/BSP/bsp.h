#ifndef __BSP_H
#define __BSP_H

#include "sys.h"
#include "gpio.h"
#include "printf.h"
#include "delay.h"
#include "led.h"
#include "key.h"
#include "usart1.h"
#include "w25q256.h"
#include "sd_card.h"
#include "adc_multi.h"
#include "rs485.h"
#include "can_bus.h"
#include "rtc_test.h"

#define DEBUG_GVAL 1         // 设为 1 时开启系统调试打印

void bsp_Init(void);

#endif 
