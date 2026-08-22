#ifndef __ADC_MULTI_H
#define __ADC_MULTI_H

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"   // 确保HAL库头文件被包含

/* ================== ADC基础配置 ================== */
/* 使用ADC1外设 */
#define ADCx                     ADC1
#define ADCx_CLK_ENABLE()        __HAL_RCC_ADC1_CLK_ENABLE()

/* ================== 通道数量定义 ================== */
/* 总共启用3个ADC通道（扫描模式） */
#define ADC_CH_COUNT             3

/* ================== 通道1：PC3（电位器） ================== */
#define ADC_CH1_GPIO_CLK()       __HAL_RCC_GPIOC_CLK_ENABLE()
#define ADC_CH1_PORT             GPIOC
#define ADC_CH1_PIN              GPIO_PIN_3
#define ADC_CH1_CHANNEL          ADC_CHANNEL_13   // PC3对应ADC1的通道13

/* ================== 通道2：PA4（杜邦线输入） ================== */
#define ADC_CH2_GPIO_CLK()       __HAL_RCC_GPIOA_CLK_ENABLE()
#define ADC_CH2_PORT             GPIOA
#define ADC_CH2_PIN              GPIO_PIN_4
#define ADC_CH2_CHANNEL          ADC_CHANNEL_4    // PA4对应ADC1的通道4

/* ================== 通道3：PA6（杜邦线输入） ================== */
#define ADC_CH3_GPIO_CLK()       __HAL_RCC_GPIOA_CLK_ENABLE() // GPIOA时钟只需开启一次，此处重复宏定义不影响
#define ADC_CH3_PORT             GPIOA
#define ADC_CH3_PIN              GPIO_PIN_6
#define ADC_CH3_CHANNEL          ADC_CHANNEL_6    // PA6对应ADC1的通道6

/* ================== DMA配置 ================== */
/*
 * STM32F4中，ADC1/2/3固定映射到DMA2
 * ADC1 → DMA2_Stream0/Stream4 (通道0)
 * 这里选择 Stream0 + Channel0
 */
#define ADC_DMA_STREAM           DMA2_Stream0
#define ADC_DMA_CHANNEL          DMA_CHANNEL_0
#define ADC_DMA_CLK_ENABLE()     __HAL_RCC_DMA2_CLK_ENABLE()

/* ================== 数据缓冲区 ================== */
/*
 * DMA循环搬运的目标数组
 * 大小为3，依次存放：CH1、CH2、CH3的转换结果
 */
#define ADC_BUF_SIZE             ADC_CH_COUNT

/* ================== 外部变量声明 ================== */
/* DMA传输完成标志（可选，当前未使用，可留作扩展） */
extern uint8_t adc_buf_ready;

/* ADC原始数据缓冲区（16位，因为ADC是12位右对齐） */
extern uint16_t adc_buf[ADC_BUF_SIZE];

/* ================== 函数声明 ================== */
void ADC_Multi_Init(void);

#endif /* __ADC_MULTI_H */