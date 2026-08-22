#ifndef __ADC_SINGLE_H
#define __ADC_SINGLE_H

#include "stm32f4xx.h"

/* ── ADC1: PC3 = CH13（电位器）── */
#define ADCx                     ADC1
#define ADCx_CLK_ENABLE()        __HAL_RCC_ADC1_CLK_ENABLE()
#define ADCx_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOC_CLK_ENABLE()
#define ADCx_CHANNEL             ADC_CHANNEL_13
#define ADCx_GPIO_PORT           GPIOC
#define ADCx_GPIO_PIN            GPIO_PIN_3

/* ── DMA: DMA2_Stream0_Ch0 ── */
#define ADC_DMA_STREAM           DMA2_Stream0
#define ADC_DMA_CHANNEL          DMA_CHANNEL_0
#define ADC_DMA_CLK_ENABLE()     __HAL_RCC_DMA2_CLK_ENABLE()

/* ── 缓冲区 ── */
#define ADC_BUF_SIZE             1
extern uint16_t adc_buf[ADC_BUF_SIZE];

void ADC_Single_Init(void);

#endif
