#include "./ADC/ADC_Multi.h"

/* 由应用层提供：DMA 回调只负责把事件转交给 AcquireTask任务。 */
extern void adc_dma_notify_from_isr(void);

/* ================== 全局变量定义 ================== */
ADC_HandleTypeDef    hadc1;           /* ADC1 句柄（HAL库核心结构体） */
DMA_HandleTypeDef    hdma_adc;        /* ADC DMA 句柄 */
uint16_t             adc_buf[ADC_BUF_SIZE]; /* DMA目标缓冲区，按扫描顺序存放两路 ADC 值 */
volatile uint8_t     adc_buf_ready = 0;     /* 1=半传输完成，2=全传输完成 */

/**
 * @brief  ADC多通道+DMA初始化函数
 * @note   配置 ADC1 扫描 PC3、PA4，通过 DMA 循环搬运数据
 */
void ADC_Multi_Init(void)
{
    /* ========== ① 开启所有相关时钟 ========== */
    ADC_CH1_GPIO_CLK();      // GPIOC时钟（PC3），电位器
    ADC_CH2_GPIO_CLK();      // GPIOA时钟（PA4），温度传感器
    ADCx_CLK_ENABLE();       // ADC1时钟
    ADC_DMA_CLK_ENABLE();    // DMA2时钟

    /* ========== ② GPIO初始化：配置为模拟输入 ========== */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_ANALOG;   // 模拟模式（ADC必须）
    gpio_init.Pull = GPIO_NOPULL;        // 浮空，不使用上下拉

    // 通道1：PC3
    gpio_init.Pin = ADC_CH1_PIN;
    HAL_GPIO_Init(ADC_CH1_PORT, &gpio_init);

    // 通道2：PA4
    gpio_init.Pin = ADC_CH2_PIN;
    HAL_GPIO_Init(ADC_CH2_PORT, &gpio_init);

    /* ========== ③ DMA配置 ========== */
    hdma_adc.Instance                 = ADC_DMA_STREAM;      // DMA2_Stream0
    hdma_adc.Init.Channel             = ADC_DMA_CHANNEL;     // Channel 0
    hdma_adc.Init.Direction           = DMA_PERIPH_TO_MEMORY;// 外设→内存
    hdma_adc.Init.PeriphInc           = DMA_PINC_DISABLE;    // 外设地址不递增（始终为ADC_DR）
    hdma_adc.Init.MemInc              = DMA_MINC_ENABLE;     // 内存地址递增（buf[0]→buf[1]→buf[2]）
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; // 外设数据16位
    hdma_adc.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD; // 内存数据16位
    hdma_adc.Init.Mode                = DMA_CIRCULAR;        // 循环模式（自动重载）
    hdma_adc.Init.Priority            = DMA_PRIORITY_HIGH;   // 高优先级
    hdma_adc.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;// 直接模式（FIFO禁用）
    HAL_DMA_Init(&hdma_adc);

    /* DMA 回调需要调用 FreeRTOS FromISR API，优先级必须不高于 5。 */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    /* 将DMA句柄绑定到ADC句柄（HAL库内部使用） */
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc);

    /* ========== ④ ADC核心参数配置 ========== */
    hadc1.Instance                   = ADCx;                  // ADC1
    hadc1.Init.ClockPrescaler        = ADC_CLOCKPRESCALER_PCLK_DIV4; // 90/4=22.5MHZ，ADC规定不要超32M
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;    // 12位分辨率（0~4095）
    hadc1.Init.ScanConvMode          = ENABLE;                // ★ 扫描模式（多通道必需）
    hadc1.Init.ContinuousConvMode    = ENABLE;                // 连续转换（不停触发）
    hadc1.Init.DiscontinuousConvMode = DISABLE;               // 不使用间断模式
    hadc1.Init.NbrOfDiscConversion   = 0;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE; // 软件触发
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;   // 右对齐（方便读取）
    hadc1.Init.NbrOfConversion       = ADC_CH_COUNT;          // 扫描两个通道
    hadc1.Init.DMAContinuousRequests = ENABLE;                // DMA连续请求
    hadc1.Init.EOCSelection          = DISABLE;               // EOC标志在每个通道转换后不置位（扫描模式推荐）
    HAL_ADC_Init(&hadc1);

    /* ========== ⑤ 配置ADC规则组通道排序 ========== */
    ADC_ChannelConfTypeDef ch_conf = {0};
    ch_conf.SamplingTime = ADC_SAMPLETIME_56CYCLES; // 采样时间56周期（兼顾速度与稳定性）
    ch_conf.Offset       = 0;

    // Rank 1 → 通道13（PC3）
    ch_conf.Channel = ADC_CH1_CHANNEL;
    ch_conf.Rank    = 1;
    HAL_ADC_ConfigChannel(&hadc1, &ch_conf);

    // Rank 2 → 通道4（PA4）
    ch_conf.Channel = ADC_CH2_CHANNEL;
    ch_conf.Rank    = 2;
    HAL_ADC_ConfigChannel(&hadc1, &ch_conf);

    /* ========== ⑥ 启动ADC+DMA ========== */
    /*
     * 启动后行为：
     * 1. ADC自动按 Rank1→Rank2 扫描
     * 2. 每转换完一个通道，DMA自动搬运到 adc_buf[]
     * 3. 完成一批扫描后触发 DMA 中断，随后继续循环覆盖缓冲区
     * 4. AcquireTask 在任务上下文中复制并平均数据
     */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
		/*
		2. (uint32_t *)adc_buf — 为什么强转 32 位
			HAL 的函数声明写死了 uint32_t *：


		HAL_ADC_Start_DMA(ADC_HandleTypeDef* hadc, uint32_t* pData, uint32_t Length);
//                                           ↑ ST 把形参类型写死了 32 位
但我们的数据 12 位存 16 位 uint16_t，不转就报警告。这个强转不影响实际搬运——DMA 的对齐宽度配的 DMA_PDATAALIGN_HALFWORD（16 位），跟 uint16_t 匹配。 ST 选 uint32_t* 是通用考量——有的 ADC 支持 16 位精度 + 过采样，结果可能是 32 位的。
		*/
}

/*
 * ================== DMA传输完成回调函数 ==================
 * HAL_DMA_IRQHandler 会在半传输和全传输时分别调用下面两个回调。
 */
/**
 * @brief  ADC DMA 半传输完成回调
 * @param  hadc ADC 句柄，用于确认本次事件是否来自 ADC1
 * @return 无
 * @note   DMA 完成前半段缓冲区后进入本函数，只记录状态并通知 AcquireTask。
 *         求平均、温度换算和寄存器更新在 AcquireTask 中完成。
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)       // 确认句柄有效，并且事件来自 ADC1
    {
        adc_buf_ready |= 1U;                         // 置位 bit0：DMA 前半区数据已经准备完成
        adc_dma_notify_from_isr();                   // 在中断中通知 AcquireTask 处理前半区数据
    }
}

/**
 * @brief  ADC DMA 全传输完成回调
 * @param  hadc ADC 句柄，用于确认本次事件是否来自 ADC1
 * @return 无
 * @note   DMA 完成整个缓冲区后进入本函数，只记录状态并通知 AcquireTask。
 *         此时后半区数据刚刚采集完成，可以复制和处理。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != NULL && hadc->Instance == ADC1)       // 确认句柄有效，并且事件来自 ADC1
    {
        adc_buf_ready |= 2U;                         // 置位 bit1：DMA 后半区数据已经准备完成
        adc_dma_notify_from_isr();                   // 在中断中通知 AcquireTask 处理后半区数据
    }
}

/*
adc_buf_ready |= 1U
→ 前半区完成
→ AcquireTask 处理 adc_buf[0] ~ adc_buf[31]

adc_buf_ready |= 2U
→ 后半区完成
→ AcquireTask 处理 adc_buf[32] ~ adc_buf[63]



*/
