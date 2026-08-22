/**
  * @file    bsp_sdio_sd.c
  * @brief   SD 卡 BSP 驱动（HAL_SD 封装 + MSP 初始化 + DMA 配置）
  *
  * 调用链：sdio_test.c → BSP_SD_xxx → HAL_SD_xxx → SDIO 硬件
  * 引脚：PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CK) PD2(CMD)
  * DMA：RX=DMA2_Stream3_CH4  TX=DMA2_Stream6_CH4
  */

#include "bsp_sdio_sd.h"

SD_HandleTypeDef  uSdHandle;                                           /* SD 卡 HAL 句柄 */
DMA_HandleTypeDef hdma_sd_tx;                                          /* SD 卡 TX DMA 句柄 */
DMA_HandleTypeDef hdma_sd_rx;                                          /* SD 卡 RX DMA 句柄 */

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_Init — 初始化 SD 卡（上电识别 + 4 线切换）
 *
 * 原型：uint8_t BSP_SD_Init(void)
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 原理：
 *   ① 配 SDIO 时钟/边沿/1线模式 → ② HAL_SD_Init（CMD0→CMD8→ACMD41→CMD2→CMD3 全链）
 *   → ③ HAL_SD_ConfigWideBusOperation 切 4 线模式
 *
 * 调用示例：
 *   if (BSP_SD_Init() == MSD_OK) { printf("SD OK\n"); }
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_Init(void)
{
    uint8_t state = MSD_OK;                                            /* 初始化状态 */

    uSdHandle.Instance = SDIO;                                         /* 指定 SDIO 外设 */

    uSdHandle.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;       /* 上升沿采样 */
    uSdHandle.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;    /* 不分频旁路 */
    uSdHandle.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;/* 不省电 */
    uSdHandle.Init.BusWide             = SDIO_BUS_WIDE_1B;             /* 先用 1 线识别卡 */
    uSdHandle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE; /* 不用硬件流控 */
    uSdHandle.Init.ClockDiv            = 0;                             /* SDIOCLK÷2 = 48÷2 = 24MHz */

    BSP_SD_MspInit(&uSdHandle, NULL);                                  /* 开时钟 + GPIO + DMA + NVIC */

    if (HAL_SD_Init(&uSdHandle) != HAL_OK)                             /* CMD0→CMD8→ACMD41→CMD2→CMD3 全链 */
    {
        state = MSD_ERROR;
    }

    if (state == MSD_OK)                                               /* 1 线识别成功后切 4 线 */
    {
        if (HAL_SD_ConfigWideBusOperation(&uSdHandle, SDIO_BUS_WIDE_4B) != HAL_OK)
        {
            state = MSD_ERROR;
        }
    }

    return state;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_ReadBlocks — 轮询读取 N 个数据块
 *
 * 原型：uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
 *       pData       — [输出] 存放读取数据的缓冲区
 *       ReadAddr    — [输入] 起始块地址
 *       NumOfBlocks — [输入] 读取块数（每块 512 字节）
 *       Timeout     — [输入] 超时时间（毫秒）
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 调用示例：
 *   BSP_SD_ReadBlocks(buf, 0, 5, 1000);  // 从块 0 读 5 块，超时 1s
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    return (HAL_SD_ReadBlocks(&uSdHandle, (uint8_t *)pData, ReadAddr, NumOfBlocks, Timeout) == HAL_OK)
           ? MSD_OK : MSD_ERROR;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_WriteBlocks — 轮询写入 N 个数据块
 *
 * 原型：uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
 *       pData       — [输入] 待写入数据的缓冲区
 *       WriteAddr   — [输入] 起始块地址
 *       NumOfBlocks — [输入] 写入块数
 *       Timeout     — [输入] 超时时间（毫秒）
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 调用示例：
 *   BSP_SD_WriteBlocks(data, 0, 5, 1000);
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    return (HAL_SD_WriteBlocks(&uSdHandle, (uint8_t *)pData, WriteAddr, NumOfBlocks, Timeout) == HAL_OK)
           ? MSD_OK : MSD_ERROR;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_ReadBlocks_DMA — DMA 读取 N 个数据块（异步，需等卡状态）
 *
 * 原型：uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
 *       pData       — [输出] 存放读取数据的缓冲区
 *       ReadAddr    — [输入] 起始块地址
 *       NumOfBlocks — [输入] 读取块数
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 调用示例：
 *   BSP_SD_ReadBlocks_DMA(buf, 0, 5); while(GetCardState != OK);
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
    return (HAL_SD_ReadBlocks_DMA(&uSdHandle, (uint8_t *)pData, ReadAddr, NumOfBlocks) == HAL_OK)
           ? MSD_OK : MSD_ERROR;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_WriteBlocks_DMA — DMA 写入 N 个数据块（异步，需等卡状态）
 *
 * 原型：uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
 *       pData       — [输入] 待写入数据的缓冲区
 *       WriteAddr   — [输入] 起始块地址
 *       NumOfBlocks — [输入] 写入块数
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 调用示例：
 *   BSP_SD_WriteBlocks_DMA(data, 0, 5); while(GetCardState != OK);
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
    return (HAL_SD_WriteBlocks_DMA(&uSdHandle, (uint8_t *)pData, WriteAddr, NumOfBlocks) == HAL_OK)
           ? MSD_OK : MSD_ERROR;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_Erase — 擦除指定范围的数据块
 *
 * 原型：uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
 *       StartAddr — [输入] 起始块地址
 *       EndAddr   — [输入] 结束块地址（块数，非字节地址）
 * 返值：MSD_OK=成功，MSD_ERROR=失败
 *
 * 调用示例：
 *   BSP_SD_Erase(0, 5);  // 擦除块 0~4
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
{
    return (HAL_SD_Erase(&uSdHandle, StartAddr, EndAddr) == HAL_OK) ? MSD_OK : MSD_ERROR;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_GetCardState — 获取卡当前状态（忙/空闲）
 *
 * 原型：uint8_t BSP_SD_GetCardState(void)
 * 返值：SD_TRANSFER_OK=空闲可操作，SD_TRANSFER_BUSY=忙等待
 *
 * 调用示例：
 *   while (BSP_SD_GetCardState() != SD_TRANSFER_OK);  // 等卡空闲
 * ════════════════════════════════════════════════════════════════
 */
uint8_t BSP_SD_GetCardState(void)
{
    return (HAL_SD_GetCardState(&uSdHandle) == HAL_SD_CARD_TRANSFER)
           ? SD_TRANSFER_OK : SD_TRANSFER_BUSY;
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_GetCardInfo — 获取 SD 卡物理信息（容量/块大小等）
 *
 * 原型：void BSP_SD_GetCardInfo(HAL_SD_CardInfoTypeDef *CardInfo)
 *       CardInfo — [输出] 存放卡信息结构体的地址
 * 返值：无
 *
 * 调用示例：
 *   HAL_SD_CardInfoTypeDef info; BSP_SD_GetCardInfo(&info);
 * ════════════════════════════════════════════════════════════════
 */
void BSP_SD_GetCardInfo(HAL_SD_CardInfoTypeDef *CardInfo)
{
    HAL_SD_GetCardInfo(&uSdHandle, CardInfo);                          /* HAL 封装，读 CID/CSD 寄存器 */
}

/* ════════════════════════════════════════════════════════════════
 * BSP_SD_MspInit — SDIO 底层硬件初始化（时钟 + GPIO + DMA + NVIC）
 *
 * 原型：void BSP_SD_MspInit(SD_HandleTypeDef *hsd, void *Params)
 *       hsd    — [输入] SD 句柄
 *       Params — [输入] 额外参数（不用，传 NULL）
 * 返值：无
 *
 * 调用的 5 步：
 *   ① 开 SDIO/DMA/GPIO 时钟
 *   ② 配 PC8~PC12 + PD2 为 AF12 推挽上拉
 *   ③ 配 SDIO 中断 NVIC
 *   ④ 配 RX DMA（卡→内存，DMA2_Stream3_CH4）
 *   ⑤ 配 TX DMA（内存→卡，DMA2_Stream6_CH4）
 *
 * 调用示例：
 *   BSP_SD_MspInit(&uSdHandle, NULL);  // BSP_SD_Init 内部调用
 * ════════════════════════════════════════════════════════════════
 */
void BSP_SD_MspInit(SD_HandleTypeDef *hsd, void *Params)
{
    GPIO_InitTypeDef gpio = {0};                                       /* GPIO 初始化结构体 */

    /* ① 开时钟 */
    __HAL_RCC_SDIO_CLK_ENABLE();                                       /* SDIO 外设时钟 */
    __DMAx_TxRx_CLK_ENABLE();                                          /* DMA2 时钟 */
    __HAL_RCC_GPIOC_CLK_ENABLE();                                      /* PC 口时钟（D0~D3+CK） */
    __HAL_RCC_GPIOD_CLK_ENABLE();                                      /* PD 口时钟（CMD） */

    /* ② 配 SDIO 引脚：复用推挽 + 上拉 + AF12 */
    gpio.Mode      = GPIO_MODE_AF_PP;                                  /* 复用推挽输出 */
    gpio.Pull      = GPIO_PULLUP;                                      /* 上拉（SD 协议要求） */
    gpio.Speed     = GPIO_SPEED_HIGH;                                  /* 高速 */
    gpio.Alternate = GPIO_AF12_SDIO;                                   /* AF12 = SDIO */

    gpio.Pin = SD_D0_PIN | SD_D1_PIN | SD_D2_PIN | SD_D3_PIN | SD_CK_PIN; /* PC8~12 */
    HAL_GPIO_Init(SD_PORT_C, &gpio);

    gpio.Pin = SD_CMD_PIN;                                             /* PD2 */
    HAL_GPIO_Init(SD_PORT_D, &gpio);

    /* ③ SDIO 中断：抢占优先级 5 */
    HAL_NVIC_SetPriority(SDIO_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    /* ④ RX DMA（卡 → 内存）：DMA2_Stream3_CH4，外设到内存，字对齐，突发 ×4，FIFO 满触发 */
    hdma_sd_rx.Init.Channel             = SD_DMAx_Rx_CHANNEL;          /* CH4 */
    hdma_sd_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;        /* 卡 → 内存 */
    hdma_sd_rx.Init.PeriphInc           = DMA_PINC_DISABLE;            /* 外设地址固定（SDIO FIFO） */
    hdma_sd_rx.Init.MemInc              = DMA_MINC_ENABLE;             /* 内存地址递增 */
    hdma_sd_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;         /* 32 位对齐 */
    hdma_sd_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;         /* 32 位对齐 */
    hdma_sd_rx.Init.Mode                = DMA_PFCTRL;                  /* 外设流控（SDIO 控制传输） */
    hdma_sd_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;      /* 最高优先级 */
    hdma_sd_rx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;         /* 开 FIFO */
    hdma_sd_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;     /* FIFO 满才触发 */
    hdma_sd_rx.Init.MemBurst            = DMA_MBURST_INC4;             /* 内存突发 ×4 */
    hdma_sd_rx.Init.PeriphBurst         = DMA_PBURST_INC4;             /* 外设突发 ×4 */
    hdma_sd_rx.Instance                 = SD_DMAx_Rx_STREAM;           /* DMA2_Stream3 */
    __HAL_LINKDMA(hsd, hdmarx, hdma_sd_rx);                           /* 绑定 RX DMA 到 SD 句柄 */
    HAL_DMA_DeInit(&hdma_sd_rx);                                       /* 先复位 */
    HAL_DMA_Init(&hdma_sd_rx);                                         /* 再初始化 */
    HAL_NVIC_SetPriority(SD_DMAx_Rx_IRQn, 6, 0);                       /* 优先级 6 */
    HAL_NVIC_EnableIRQ(SD_DMAx_Rx_IRQn);

    /* ⑤ TX DMA（内存 → 卡）：DMA2_Stream6_CH4，配置同 RX 反向 */
    hdma_sd_tx.Init.Channel             = SD_DMAx_Tx_CHANNEL;          /* CH4 */
    hdma_sd_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;        /* 内存 → 卡 */
    hdma_sd_tx.Init.PeriphInc           = DMA_PINC_DISABLE;            /* 外设地址固定 */
    hdma_sd_tx.Init.MemInc              = DMA_MINC_ENABLE;             /* 内存地址递增 */
    hdma_sd_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sd_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma_sd_tx.Init.Mode                = DMA_PFCTRL;
    hdma_sd_tx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    hdma_sd_tx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    hdma_sd_tx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_sd_tx.Init.MemBurst            = DMA_MBURST_INC4;
    hdma_sd_tx.Init.PeriphBurst         = DMA_PBURST_INC4;
    hdma_sd_tx.Instance                 = SD_DMAx_Tx_STREAM;           /* DMA2_Stream6 */
    __HAL_LINKDMA(hsd, hdmatx, hdma_sd_tx);                           /* 绑定 TX DMA 到 SD 句柄 */
    HAL_DMA_DeInit(&hdma_sd_tx);
    HAL_DMA_Init(&hdma_sd_tx);
    HAL_NVIC_SetPriority(SD_DMAx_Tx_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SD_DMAx_Tx_IRQn);
}
