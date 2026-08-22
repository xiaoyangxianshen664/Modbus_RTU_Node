/**
  * @file    bsp_sdio_sd.h
  * @brief   SD 卡 BSP 驱动头文件 — 引脚/DMA 宏定义 + API 声明 + 外部句柄
  *
  * 引脚映射：PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CK) PD2(CMD)
  * DMA 配置：RX=DMA2_Stream3_CH4  TX=DMA2_Stream6_CH4
  */

#ifndef __BSP_SDIO_SD_H
#define __BSP_SDIO_SD_H

#include "stm32f4xx.h"

/* ── 状态码 ── */
#define MSD_OK              0x00                                        /* 操作成功 */
#define MSD_ERROR           0x01                                        /* 操作失败 */
#define SD_TRANSFER_OK      ((uint8_t)0x00)                             /* 卡空闲，可操作 */
#define SD_TRANSFER_BUSY    ((uint8_t)0x01)                             /* 卡忙，需等待 */

/* ── 超时 ── */
#define SD_DATATIMEOUT      ((uint32_t)100000000)                       /* 读写超时（100 秒，实际不会用满） */

/* ── SDIO 引脚 — 4 线模式 ── */
#define SD_D0_PIN    GPIO_PIN_8                                         /* PC8  = D0（数据线 0） */
#define SD_D1_PIN    GPIO_PIN_9                                         /* PC9  = D1 */
#define SD_D2_PIN    GPIO_PIN_10                                        /* PC10 = D2 */
#define SD_D3_PIN    GPIO_PIN_11                                        /* PC11 = D3 */
#define SD_CK_PIN    GPIO_PIN_12                                        /* PC12 = CK（时钟） */
#define SD_CMD_PIN   GPIO_PIN_2                                         /* PD2  = CMD（命令/响应） */
#define SD_PORT_C    GPIOC                                              /* D0~D3 + CK 所在的端口 */
#define SD_PORT_D    GPIOD                                              /* CMD 所在的端口 */
#define SD_AF        GPIO_AF12_SDIO                                     /* 复用功能 AF12 = SDIO */

/* ── DMA 配置 ── */
#define __DMAx_TxRx_CLK_ENABLE     __HAL_RCC_DMA2_CLK_ENABLE           /* DMA2 时钟使能 */
#define SD_DMAx_Tx_CHANNEL         DMA_CHANNEL_4                        /* TX 通道 4 */
#define SD_DMAx_Rx_CHANNEL         DMA_CHANNEL_4                        /* RX 通道 4（同通道，不同流） */
#define SD_DMAx_Tx_STREAM          DMA2_Stream6                         /* TX 流：DMA2_Stream6 */
#define SD_DMAx_Rx_STREAM          DMA2_Stream3                         /* RX 流：DMA2_Stream3 */
#define SD_DMAx_Tx_IRQn            DMA2_Stream6_IRQn                    /* TX 中断号 */
#define SD_DMAx_Rx_IRQn            DMA2_Stream3_IRQn                    /* RX 中断号 */

/* ── 外部句柄（bsp_sdio_sd.c 中定义） ── */
extern SD_HandleTypeDef  uSdHandle;                                     /* SD 卡 HAL 句柄 */
extern DMA_HandleTypeDef hdma_sd_tx;                                    /* SD 卡 TX DMA 句柄 */
extern DMA_HandleTypeDef hdma_sd_rx;                                    /* SD 卡 RX DMA 句柄 */

/* ── API（实现见 bsp_sdio_sd.c） ── */
uint8_t BSP_SD_Init(void);                                              /* 初始化 SD 卡（上电识别 + 4 线切换） */
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr,          /* 轮询读块 */
                           uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr,        /* 轮询写块 */
                            uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr,      /* DMA 读块（异步） */
                               uint32_t NumOfBlocks);
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr,    /* DMA 写块（异步） */
                                uint32_t NumOfBlocks);
uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr);            /* 擦除指定块范围 */
uint8_t BSP_SD_GetCardState(void);                                      /* 获取卡状态（忙/空闲） */
void    BSP_SD_GetCardInfo(HAL_SD_CardInfoTypeDef *CardInfo);          /* 获取卡物理信息 */
void    BSP_SD_MspInit(SD_HandleTypeDef *hsd, void *Params);           /* 底层硬件初始化（GPIO/DMA/NVIC） */
void    BSP_SD_IRQHandler(void);                                        /* SDIO 中断处理 */
void    BSP_SD_DMA_Tx_IRQHandler(void);                                 /* TX DMA 中断处理 */
void    BSP_SD_DMA_Rx_IRQHandler(void);                                 /* RX DMA 中断处理 */

#endif
