#ifndef __SD_CARD_H
#define __SD_CARD_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define SD_CARD_BLOCK_SIZE          512U
#define SD_CARD_IO_TIMEOUT_MS       3000U
#define SD_CARD_READY_TIMEOUT_MS    3000U

extern SD_HandleTypeDef g_sd_card;
extern volatile uint8_t g_sd_init_stage;

HAL_StatusTypeDef SD_Card_Init(void);
HAL_StatusTypeDef SD_Card_ReadBlocks(uint8_t *data,
                                     uint32_t block_address,
                                     uint32_t block_count);
HAL_StatusTypeDef SD_Card_WriteBlocks(const uint8_t *data,
                                      uint32_t block_address,
                                      uint32_t block_count);
HAL_StatusTypeDef SD_Card_GetInfo(HAL_SD_CardInfoTypeDef *card_info);
HAL_StatusTypeDef SD_Card_WaitReady(uint32_t timeout_ms);
void SD_Card_SDIO_IRQHandler(void);
void SD_Card_DMA_IRQHandler(void);

#endif /* __SD_CARD_H */
