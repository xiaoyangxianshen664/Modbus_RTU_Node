#ifndef __W25Q256_H
#define __W25Q256_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define W25Q256_JEDEC_ID             0xEF4019UL
#define W25Q256_PAGE_SIZE            256U
#define W25Q256_SECTOR_SIZE          4096UL
#define W25Q256_DMA_MAX_TRANSFER     W25Q256_PAGE_SIZE

extern SPI_HandleTypeDef g_w25q256_spi;
extern DMA_HandleTypeDef g_w25q256_tx_dma;
extern DMA_HandleTypeDef g_w25q256_rx_dma;

HAL_StatusTypeDef W25Q256_Init(void);
uint32_t W25Q256_ReadJedecId(void);
HAL_StatusTypeDef W25Q256_EraseSector(uint32_t address);
HAL_StatusTypeDef W25Q256_WriteDMA(const uint8_t *data, uint32_t address, uint32_t length);
HAL_StatusTypeDef W25Q256_ReadDMA(uint8_t *data, uint32_t address, uint16_t length);
void W25Q256_TX_DMA_IRQHandler(void);
void W25Q256_RX_DMA_IRQHandler(void);

#endif /* __W25Q256_H */
