#include "./ADC/ADC_Multi.h"

/* ================== 全局变量定义 ================== */
ADC_HandleTypeDef    hadc1;           /* ADC1 句柄（HAL库核心结构体） */
DMA_HandleTypeDef    hdma_adc;        /* ADC DMA 句柄 */
uint16_t             adc_buf[ADC_BUF_SIZE]; /* DMA目标缓冲区，存放3路ADC值 */
uint8_t              adc_buf_ready = 0;     /* DMA传输完成标志（可用于中断回调） */

/**
 * @brief  ADC多通道+DMA初始化函数
 * @note   配置ADC1扫描3个通道，通过DMA循环搬运数据
 */
void ADC_Multi_Init(void)
{
    /* ========== ① 开启所有相关时钟 ========== */
    ADC_CH1_GPIO_CLK();      // GPIOC时钟（PC3）
    ADC_CH2_GPIO_CLK();      // GPIOA时钟（PA4、PA6）
    ADC_CH3_GPIO_CLK();      // GPIOA时钟（重复开启无影响）
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

    // 通道3：PA6
    gpio_init.Pin = ADC_CH3_PIN;
    HAL_GPIO_Init(ADC_CH3_PORT, &gpio_init);

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
    hadc1.Init.NbrOfConversion       = ADC_CH_COUNT;          // ★ 转换通道数 = 3
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

    // Rank 3 → 通道6（PA6）
    ch_conf.Channel = ADC_CH3_CHANNEL;
    ch_conf.Rank    = 3;
    HAL_ADC_ConfigChannel(&hadc1, &ch_conf);

    /* ========== ⑥ 启动ADC+DMA ========== */
    /*
     * 启动后行为：
     * 1. ADC自动按 Rank1→Rank2→Rank3 扫描
     * 2. 每转换完一个通道，DMA自动搬运到 adc_buf[]
     * 3. 转换完3个通道后，DMA自动回到buf起始地址（循环）
     * 4. 主循环中可直接读取 adc_buf[0]~[2]
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
 * ================== 可选：DMA传输完成回调函数 ==================
 * 如果需要半传输/全传输中断，可取消注释并在main中启用
 */
/*
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_buf_ready = 1;  // 标记数据已更新
    }
}
*/