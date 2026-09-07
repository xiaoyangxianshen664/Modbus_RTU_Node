#include "sd_card.h"

SD_HandleTypeDef g_sd_card;
volatile uint8_t g_sd_init_stage;

static DMA_HandleTypeDef g_sd_dma;
static volatile uint8_t g_sd_dma_done;
static volatile uint8_t g_sd_dma_error;
static uint8_t g_sd_dma_ready;

/**
 * @brief  等待SD卡结束内部编程并回到可传输状态
 * @param  timeout_ms [输入] 最长等待时间, 单位毫秒
 * @return HAL_OK表示卡已经就绪; HAL_TIMEOUT表示等待超时
 * @note   写操作结束后卡可能仍忙, 必须等到TRANSFER状态才能继续访问.
 * @example SD_Card_WaitReady(3000U);
 */
HAL_StatusTypeDef SD_Card_WaitReady(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while (HAL_SD_GetCardState(&g_sd_card) != HAL_SD_CARD_TRANSFER)
    {
        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

/**
 * @brief  根据本次读写方向重新配置F103的SDIO DMA通道
 * @param  direction [输入] DMA_PERIPH_TO_MEMORY或DMA_MEMORY_TO_PERIPH
 * @return HAL_OK表示DMA配置成功; HAL_ERROR表示底层初始化失败
 * @note   F103的SDIO读写共用DMA2 Channel4, 每次传输前必须切换方向.
 * @example SD_Card_ConfigDMA(DMA_PERIPH_TO_MEMORY);
 */
static HAL_StatusTypeDef SD_Card_ConfigDMA(uint32_t direction)
{
    if (g_sd_dma_ready == 0U)
    {
        return HAL_ERROR;
    }

    (void)HAL_DMA_DeInit(&g_sd_dma);
    g_sd_dma.Init.Direction = direction;
    g_sd_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_sd_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_sd_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_sd_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    g_sd_dma.Init.Mode = DMA_NORMAL;
    g_sd_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;

    if (HAL_DMA_Init(&g_sd_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_sd_dma.Parent = &g_sd_card;
    return HAL_OK;
}

/**
 * @brief  等待一次SDIO DMA传输完成
 * @param  timeout_ms [输入] 最长等待时间, 单位毫秒
 * @return HAL_OK表示DMA及卡状态正常; HAL_ERROR表示中断报告错误; HAL_TIMEOUT表示超时
 * @note   完成标志由HAL_SD_RxCpltCallback或HAL_SD_TxCpltCallback在中断中置位.
 * @example SD_Card_WaitDMA(3000U);
 */
static HAL_StatusTypeDef SD_Card_WaitDMA(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while ((g_sd_dma_done == 0U) && (g_sd_dma_error == 0U))
    {
        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            (void)HAL_SD_Abort(&g_sd_card);
            g_sd_card.ErrorCode |= HAL_SD_ERROR_TIMEOUT;
            return HAL_TIMEOUT;
        }
    }

    if ((g_sd_dma_error != 0U) ||
        (g_sd_card.ErrorCode != HAL_SD_ERROR_NONE))
    {
        return HAL_ERROR;
    }
    return SD_Card_WaitReady(SD_CARD_READY_TIMEOUT_MS);
}

/**
 * @brief  初始化RCT6的SDIO、DMA及SD卡
 * @param  无
 * @return HAL_OK表示识别并切换到4位总线成功; 其他值表示初始化失败
 * @note   识别阶段固定约400kHz, 数据阶段先使用12MHz验证DMA通路.
 * @example status = SD_Card_Init();
 */
HAL_StatusTypeDef SD_Card_Init(void)
{
    HAL_StatusTypeDef result;

    g_sd_init_stage = 1U;  // 阶段1: 开始配置并执行HAL_SD_Init
    g_sd_card.Instance = SDIO;
    g_sd_card.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    g_sd_card.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    g_sd_card.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    g_sd_card.Init.BusWide = SDIO_BUS_WIDE_1B;
    g_sd_card.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    g_sd_card.Init.ClockDiv = 4U;  // 72MHz / (4 + 2) = 12MHz

    if (HAL_SD_Init(&g_sd_card) != HAL_OK)
    {
        g_sd_init_stage = 2U;  // 阶段2: 1位模式卡识别失败
        return HAL_ERROR;
    }
    if (g_sd_dma_ready == 0U)
    {
        g_sd_card.ErrorCode |= HAL_SD_ERROR_DMA;
        return HAL_ERROR;
    }

    g_sd_init_stage = 3U;  // 阶段3: 1位识别成功, 准备发送ACMD6切换4位
    if (HAL_SD_ConfigWideBusOperation(&g_sd_card, SDIO_BUS_WIDE_4B) != HAL_OK)
    {
        g_sd_init_stage = 4U;  // 阶段4: ACMD6或切换4位失败
        return HAL_ERROR;
    }

    g_sd_init_stage = 5U;  // 阶段5: 已配置4位总线, 等待卡回到TRANSFER状态
    result = SD_Card_WaitReady(SD_CARD_READY_TIMEOUT_MS);
    if (result == HAL_OK)
    {
        g_sd_init_stage = 6U;  // 阶段6: SD卡及DMA初始化全部完成
    }
    return result;
}

/**
 * @brief  使用DMA读取一个或多个512字节SD卡逻辑块
 * @param  data [输出] 保存读取数据的缓冲区, 地址必须按4字节对齐
 * @param  block_address [输入] 起始逻辑块地址, 不是字节地址
 * @param  block_count [输入] 连续读取的块数
 * @return HAL_OK表示DMA读取完成; 其他值表示参数、启动、通信或超时错误
 * @note   函数内部等待中断完成, 对FatFs保持同步接口.
 * @example SD_Card_ReadBlocks(buffer, 0U, 1U);
 */
HAL_StatusTypeDef SD_Card_ReadBlocks(uint8_t *data,
                                     uint32_t block_address,
                                     uint32_t block_count)
{
    HAL_StatusTypeDef result;

    if ((data == NULL) || (block_count == 0U) ||
        ((((uint32_t)data) & 0x03U) != 0U))
    {
        return HAL_ERROR;
    }
    result = SD_Card_WaitReady(SD_CARD_READY_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }
    if (SD_Card_ConfigDMA(DMA_PERIPH_TO_MEMORY) != HAL_OK)
    {
        g_sd_card.ErrorCode |= HAL_SD_ERROR_DMA;
        return HAL_ERROR;
    }

    g_sd_dma_done = 0U;
    g_sd_dma_error = 0U;
    result = HAL_SD_ReadBlocks_DMA(&g_sd_card,
                                   data,
                                   block_address,
                                   block_count);
    if (result != HAL_OK)
    {
        return result;
    }
    return SD_Card_WaitDMA(SD_CARD_IO_TIMEOUT_MS);
}

/**
 * @brief  使用DMA写入一个或多个512字节SD卡逻辑块
 * @param  data [输入] 待写数据缓冲区, 地址必须按4字节对齐
 * @param  block_address [输入] 起始逻辑块地址, 不是字节地址
 * @param  block_count [输入] 连续写入的块数
 * @return HAL_OK表示DMA写入并等待卡编程完成; 其他值表示错误
 * @note   F103读写共用DMA2 Channel4, 此处在启动前切换为内存到外设方向.
 * @example SD_Card_WriteBlocks(buffer, 10U, 1U);
 */
HAL_StatusTypeDef SD_Card_WriteBlocks(const uint8_t *data,
                                      uint32_t block_address,
                                      uint32_t block_count)
{
    HAL_StatusTypeDef result;

    if ((data == NULL) || (block_count == 0U) ||
        ((((uint32_t)data) & 0x03U) != 0U))
    {
        return HAL_ERROR;
    }
    result = SD_Card_WaitReady(SD_CARD_READY_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }
    if (SD_Card_ConfigDMA(DMA_MEMORY_TO_PERIPH) != HAL_OK)
    {
        g_sd_card.ErrorCode |= HAL_SD_ERROR_DMA;
        return HAL_ERROR;
    }

    g_sd_dma_done = 0U;
    g_sd_dma_error = 0U;
    result = HAL_SD_WriteBlocks_DMA(&g_sd_card,
                                    (uint8_t *)data,
                                    block_address,
                                    block_count);
    if (result != HAL_OK)
    {
        return result;
    }
    return SD_Card_WaitDMA(SD_CARD_IO_TIMEOUT_MS);
}

/**
 * @brief  读取SD卡类型、容量和逻辑块大小
 * @param  card_info [输出] 保存HAL卡信息结构体
 * @return HAL_OK表示获取成功; HAL_ERROR表示参数无效或HAL读取失败
 * @note   容量应使用LogBlockNbr和LogBlockSize计算.
 * @example SD_Card_GetInfo(&card_info);
 */
HAL_StatusTypeDef SD_Card_GetInfo(HAL_SD_CardInfoTypeDef *card_info)
{
    if (card_info == NULL)
    {
        return HAL_ERROR;
    }
    return HAL_SD_GetCardInfo(&g_sd_card, card_info);
}

/**
 * @brief  配置STM32F103RCT6的SDIO、DMA2 Channel4及NVIC
 * @param  hsd [输入输出] HAL传入的SD卡句柄
 * @return 无
 * @note   PC8至PC11为D0至D3, PC12为CK, PD2为CMD; DMA读写共用Channel4.
 * @example 由HAL_SD_Init()内部自动调用.
 */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio = {0};

    if (hsd->Instance != SDIO)
    {
        return;
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    gpio.Pin = RCT6_SD_D0_PIN | RCT6_SD_D1_PIN |
               RCT6_SD_D2_PIN | RCT6_SD_D3_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = RCT6_SD_CK_PIN;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(RCT6_SD_CK_PORT, &gpio);

    gpio.Pin = RCT6_SD_CMD_PIN;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RCT6_SD_CMD_PORT, &gpio);

    g_sd_dma.Instance = DMA2_Channel4;
    g_sd_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_sd_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_sd_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_sd_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_sd_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    g_sd_dma.Init.Mode = DMA_NORMAL;
    g_sd_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;

    (void)HAL_DMA_DeInit(&g_sd_dma);
    if (HAL_DMA_Init(&g_sd_dma) == HAL_OK)
    {
        g_sd_dma_ready = 1U;
    }
    else
    {
        g_sd_dma_ready = 0U;
    }
    __HAL_LINKDMA(hsd, hdmarx, g_sd_dma);
    __HAL_LINKDMA(hsd, hdmatx, g_sd_dma);

    HAL_NVIC_SetPriority(SDIO_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
    HAL_NVIC_SetPriority(DMA2_Channel4_5_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);
}

/**
 * @brief  转交SDIO全局中断给HAL SD驱动
 * @param  无
 * @return 无
 * @note   DATAEND及总线错误均由HAL_SD_IRQHandler处理.
 * @example 在SDIO_IRQHandler()中调用.
 */
void SD_Card_SDIO_IRQHandler(void)
{
    HAL_SD_IRQHandler(&g_sd_card);
}

/**
 * @brief  转交DMA2 Channel4中断给HAL DMA驱动
 * @param  无
 * @return 无
 * @note   RCT6启动文件将DMA2 Channel4和Channel5共用一个中断入口.
 * @example 在DMA2_Channel4_5_IRQHandler()中调用.
 */
void SD_Card_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_sd_dma);
}

/**
 * @brief  HAL报告SD DMA读取完成
 * @param  hsd [输入] 触发回调的SD句柄
 * @return 无
 * @note   仅设置标志, 中断中不执行阻塞操作.
 * @example 由HAL内部自动调用.
 */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &g_sd_card)
    {
        g_sd_dma_done = 1U;
    }
}

/**
 * @brief  HAL报告SD DMA写入传输完成
 * @param  hsd [输入] 触发回调的SD句柄
 * @return 无
 * @note   随后同步接口还会等待SD卡内部编程结束.
 * @example 由HAL内部自动调用.
 */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &g_sd_card)
    {
        g_sd_dma_done = 1U;
    }
}

/**
 * @brief  HAL报告SDIO或DMA传输错误
 * @param  hsd [输入] 触发回调的SD句柄
 * @return 无
 * @note   等待函数检测到错误标志后退出, 防止FatFs永久阻塞.
 * @example 由HAL内部自动调用.
 */
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &g_sd_card)
    {
        g_sd_dma_error = 1U;
    }
}
