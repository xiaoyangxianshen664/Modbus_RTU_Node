#ifndef __USART1_H
#define __USART1_H

#include "stm32f1xx_hal.h"

#define USART1_RX_DMA_BUFFER_SIZE 128U
#define USART1_TX_DMA_BUFFER_SIZE 200U

extern UART_HandleTypeDef g_usart1_uart;
extern DMA_HandleTypeDef g_usart1_tx_dma;
extern DMA_HandleTypeDef g_usart1_rx_dma;

void USART1_BSP_Init(uint32_t baudrate);
HAL_StatusTypeDef USART1_SendDMA(const uint8_t *data, uint16_t length);
uint16_t USART1_ReadFrame(uint8_t *destination, uint16_t capacity);
void USART1_BSP_IRQHandler(void);
void USART1_TX_DMA_IRQHandler(void);
void USART1_RX_DMA_IRQHandler(void);

#endif /* __USART1_H */
