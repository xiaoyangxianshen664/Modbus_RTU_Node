#include "adc_multi.h"

ADC_HandleTypeDef g_adc1;
volatile uint32_t g_adc_dma_cycle_count;

static DMA_HandleTypeDef g_adc_dma;
static uint16_t g_adc_buffer[ADC_MULTI_BUFFER_LENGTH];
static volatile uint8_t g_adc_ready_half;

/**
 * @brief  初始化ADC1双通道扫描和DMA循环采集
 * @param  无
 * @return HAL_OK表示ADC校准、通道配置和DMA启动成功; 其他值表示失败
 * @note   Rank1采集PC3/IN13, Rank2采集PC4/IN14; DMA1 Channel1循环搬运.
 * @example if (ADC_Multi_Init() != HAL_OK) { Error_Handler(); }
 */
HAL_StatusTypeDef ADC_Multi_Init(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    g_adc1.Instance = ADC1;
    g_adc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    g_adc1.Init.ContinuousConvMode = ENABLE;
    g_adc1.Init.DiscontinuousConvMode = DISABLE;
    g_adc1.Init.NbrOfDiscConversion = 1U;
    g_adc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    g_adc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    g_adc1.Init.NbrOfConversion = ADC_MULTI_CHANNEL_COUNT;

    if (HAL_ADC_Init(&g_adc1) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_ADCEx_Calibration_Start(&g_adc1) != HAL_OK)
    {
        return HAL_ERROR;
    }

    channel.Channel = RCT6_ADC1_CHANNEL;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&g_adc1, &channel) != HAL_OK)
    {
        return HAL_ERROR;
    }

    channel.Channel = RCT6_ADC2_CHANNEL;
    channel.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&g_adc1, &channel) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_adc_ready_half = 0U;
    g_adc_dma_cycle_count = 0U;
    return HAL_ADC_Start_DMA(&g_adc1,
                             (uint32_t *)g_adc_buffer,
                             ADC_MULTI_BUFFER_LENGTH);
}

/**
 * @brief  对DMA最近完成的半区数据分别求两路平均值
 * @param  adc1_average [输出] PC3/IN13的12位平均值
 * @param  adc2_average [输出] PC4/IN14的12位平均值
 * @return 1表示获得一批新数据; 0表示参数无效或尚无新数据
 * @note   ADC扫描结果按PC3、PC4、PC3、PC4的顺序交错保存在缓冲区.
 * @example ADC_Multi_GetAverage(&pc3_raw, &pc4_raw);
 */
uint8_t ADC_Multi_GetAverage(uint16_t *adc1_average,
                             uint16_t *adc2_average)
{
    uint32_t adc1_sum = 0U;
    uint32_t adc2_sum = 0U;
    uint32_t start_index;
    uint32_t index;
    uint8_t ready_half;

    if ((adc1_average == NULL) || (adc2_average == NULL))
    {
        return 0U;
    }

    __disable_irq();
    ready_half = g_adc_ready_half;
    g_adc_ready_half = 0U;
    __enable_irq();

    if (ready_half == 0U)
    {
        return 0U;
    }

    start_index = (ready_half == 1U) ? 0U : (ADC_MULTI_BUFFER_LENGTH / 2U);
    for (index = 0U;
         index < (ADC_MULTI_SAMPLES_PER_CHANNEL / 2U);
         ++index)
    {
        adc1_sum += g_adc_buffer[start_index + index * ADC_MULTI_CHANNEL_COUNT];
        adc2_sum += g_adc_buffer[start_index + index * ADC_MULTI_CHANNEL_COUNT + 1U];
    }

    *adc1_average = (uint16_t)(adc1_sum / (ADC_MULTI_SAMPLES_PER_CHANNEL / 2U));
    *adc2_average = (uint16_t)(adc2_sum / (ADC_MULTI_SAMPLES_PER_CHANNEL / 2U));
    return 1U;
}

/**
 * @brief  配置ADC1输入GPIO、ADC时钟和DMA1 Channel1
 * @param  hadc [输入输出] HAL传入的ADC句柄
 * @return 无
 * @note   PCLK2为72MHz, 六分频后ADC时钟为12MHz, 不超过F103的14MHz上限.
 * @example 由HAL_ADC_Init()内部自动调用.
 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef gpio = {0};

    if (hadc->Instance != ADC1)
    {
        return;
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    gpio.Pin = RCT6_ADC1_PIN | RCT6_ADC2_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    g_adc_dma.Instance = DMA1_Channel1;
    g_adc_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_adc_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_adc_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_adc_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    g_adc_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    g_adc_dma.Init.Mode = DMA_CIRCULAR;
    g_adc_dma.Init.Priority = DMA_PRIORITY_HIGH;
    (void)HAL_DMA_DeInit(&g_adc_dma);
    (void)HAL_DMA_Init(&g_adc_dma);
    __HAL_LINKDMA(hadc, DMA_Handle, g_adc_dma);

    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/**
 * @brief  转交ADC1固定使用的DMA1 Channel1中断给HAL
 * @param  无
 * @return 无
 * @note   HAL随后调用半传输或全传输完成回调.
 * @example 在DMA1_Channel1_IRQHandler()中调用.
 */
void ADC_Multi_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_adc_dma);
}

/**
 * @brief  ADC DMA前半区采集完成回调
 * @param  hadc [输入] 触发回调的ADC句柄
 * @return 无
 * @note   仅记录可读取的半区, 不在中断中计算平均值或打印.
 * @example 由HAL内部自动调用.
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &g_adc1)
    {
        g_adc_ready_half = 1U;
    }
}

/**
 * @brief  ADC DMA后半区采集完成回调
 * @param  hadc [输入] 触发回调的ADC句柄
 * @return 无
 * @note   每完成整个缓冲区将循环计数加一, 便于确认DMA持续运行.
 * @example 由HAL内部自动调用.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &g_adc1)
    {
        g_adc_ready_half = 2U;
        ++g_adc_dma_cycle_count;
    }
}
