#ifndef __ADC_MULTI_H
#define __ADC_MULTI_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define ADC_MULTI_CHANNEL_COUNT        2U
#define ADC_MULTI_SAMPLES_PER_CHANNEL  32U
#define ADC_MULTI_BUFFER_LENGTH        (ADC_MULTI_CHANNEL_COUNT * ADC_MULTI_SAMPLES_PER_CHANNEL)

extern ADC_HandleTypeDef g_adc1;
extern volatile uint32_t g_adc_dma_cycle_count;

HAL_StatusTypeDef ADC_Multi_Init(void);
uint8_t ADC_Multi_GetAverage(uint16_t *adc1_average,
                             uint16_t *adc2_average);
void ADC_Multi_DMA_IRQHandler(void);

#endif /* __ADC_MULTI_H */
