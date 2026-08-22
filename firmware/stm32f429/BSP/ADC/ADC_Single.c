#include "./ADC/ADC_Single.h"   /* 包含 ADC 单通道模块的头文件，提供宏定义与函数声明 */

ADC_HandleTypeDef    hadc1;         /* ADC1 句柄 */
DMA_HandleTypeDef    hdma_adc;      /* ADC DMA 句柄 */
uint16_t             adc_buf[ADC_BUF_SIZE];  /* DMA 搬运目标 */

void ADC_Single_Init(void)          /* ADC 单通道 + DMA 初始化函数 */
{
    /* ① 开时钟 */
    ADCx_GPIO_CLK_ENABLE();   /* GPIOC 时钟使能 */
    ADCx_CLK_ENABLE();        /* ADC1 时钟使能 */
    ADC_DMA_CLK_ENABLE();     /* DMA2 时钟使能 */

    /* ② GPIO：模拟模式 */
    GPIO_InitTypeDef g = {0};                     /* 定义 GPIO 初始化结构体并清零 */
    g.Mode  = GPIO_MODE_ANALOG;                  /* 引脚设为模拟输入模式 */
    g.Pull  = GPIO_NOPULL;                       /* 无上拉、无下拉 */
    g.Pin   = ADCx_GPIO_PIN;                     /* 指定 ADC 对应的 GPIO 引脚 */
    HAL_GPIO_Init(ADCx_GPIO_PORT, &g);            /* 初始化 GPIO */

    /* ③ DMA：外设→内存，循环模式 */
    hdma_adc.Instance                 = ADC_DMA_STREAM;       /* 指定 DMA 流（如 DMA2_Stream0） */
    hdma_adc.Init.Channel             = ADC_DMA_CHANNEL;      /* 指定 DMA 通道号（如 CH0） */
    hdma_adc.Init.Direction           = DMA_PERIPH_TO_MEMORY; /* 传输方向：外设到内存 */
    hdma_adc.Init.PeriphInc           = DMA_PINC_DISABLE;     /* 外设地址不递增（ADC 数据寄存器固定） */
    hdma_adc.Init.MemInc              = DMA_MINC_ENABLE;      /* 内存地址递增（数组下标自动 +1） */
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;  /* 16 位 */
    hdma_adc.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;  /* 内存数据对齐半字（16 位） */
    hdma_adc.Init.Mode                = DMA_CIRCULAR;              /* 循环搬 */
    hdma_adc.Init.Priority            = DMA_PRIORITY_HIGH;         /* DMA 优先级为高 */
    hdma_adc.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;      /* 禁用 FIFO，直接传输 */
    HAL_DMA_Init(&hdma_adc);                                      /* 初始化 DMA */

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc);  /* 挂到 ADC 句柄上 */

    /* ④ ADC 句柄 */
    hadc1.Instance                   = ADCx;                           /* 使用 ADC1 */
    hadc1.Init.ClockPrescaler        = ADC_CLOCKPRESCALER_PCLK_DIV4;   /* 90/4=22.5MHz */
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;              /* 12 位 转换结果 0~4095，存 `uint16_t`。*/
    hadc1.Init.ScanConvMode          = DISABLE;                        /* 非扫描模式，我们单通道 */
    hadc1.Init.ContinuousConvMode    = ENABLE;                         /* 连续转换 */
    hadc1.Init.DiscontinuousConvMode = DISABLE;                        //间断模式
    hadc1.Init.NbrOfDiscConversion   = 0;                             //每次触发间断转换的通道数（1~8）
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;  //ADC 不会因为定时器更新、外部引脚电平变化等硬件事件而启动转换。
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;             //由软件调用 HAL_ADC_Start() / HAL_ADC_Start_IT() 等函数来启动转换。
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;            /* 右对齐：`RIGHT`（右对齐）。12 位数据放 16 位寄存器，右对齐直接当 0~4095 整数用。 */
    hadc1.Init.NbrOfConversion       = 1;                              /* 1 个通道 */
    hadc1.Init.DMAContinuousRequests = ENABLE;                         /* DMA 持续请求 ：不开则 DMA 搬完一轮就停，开了才能配合 CIRCULAR 模式永续工作。*/
    hadc1.Init.EOCSelection          = DISABLE;                        //转换结束标志位，DMA 模式下不用关心此标志，HAL 内部处理。
    HAL_ADC_Init(&hadc1);                                              /* 初始化 ADC */

    /* ⑤ 通道配置 */
    ADC_ChannelConfTypeDef ch = {0};      /* 定义 ADC 通道配置结构体并清零 */
    ch.Channel      = ADCx_CHANNEL;       /* 选择 ADC 通道（如 CH10 对应 PC0） */
    ch.Rank         = 1;                  /* 规则组转换序列中排第 1 位 */
    ch.SamplingTime = ADC_SAMPLETIME_56CYCLES; /* 采样时间 56 个 ADC 时钟周期 */
    ch.Offset       = 0;                  /* 转换结果无硬件偏移 */
    HAL_ADC_ConfigChannel(&hadc1, &ch);   /* 配置 ADC 通道 */

    /* ⑥ 启动 ADC DMA */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE); /* 启动 ADC，并通过 DMA 循环搬运转换结果到 adc_buf */
}