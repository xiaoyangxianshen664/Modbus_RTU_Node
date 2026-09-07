#ifndef __EEPROM_H
#define __EEPROM_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define EEPROM_CAPACITY_BYTES     256U
#define EEPROM_PAGE_SIZE_BYTES    8U
#define EEPROM_DMA_TIMEOUT_MS     100U

extern I2C_HandleTypeDef g_eeprom_i2c;
extern DMA_HandleTypeDef g_eeprom_tx_dma;
extern DMA_HandleTypeDef g_eeprom_rx_dma;

HAL_StatusTypeDef EEPROM_Init(void);
HAL_StatusTypeDef EEPROM_IsReady(void);
HAL_StatusTypeDef EEPROM_WriteDMA(uint8_t address,
                                  const uint8_t *data,
                                  uint16_t length);
HAL_StatusTypeDef EEPROM_ReadDMA(uint8_t address,
                                 uint8_t *data,
                                 uint16_t length);
void EEPROM_I2C_EV_IRQHandler(void);
void EEPROM_I2C_ER_IRQHandler(void);
void EEPROM_TX_DMA_IRQHandler(void);
void EEPROM_RX_DMA_IRQHandler(void);

#endif /* __EEPROM_H */
