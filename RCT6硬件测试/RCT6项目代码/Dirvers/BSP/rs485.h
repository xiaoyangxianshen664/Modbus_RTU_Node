#ifndef __RS485_H
#define __RS485_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define RS485_BAUDRATE          9600U
#define RS485_RX_BUFFER_SIZE    256U

extern UART_HandleTypeDef g_rs485_uart;

HAL_StatusTypeDef RS485_Init(void);
HAL_StatusTypeDef RS485_Send(const uint8_t *data, uint16_t length);
uint16_t RS485_ReadFrame(uint8_t *destination, uint16_t capacity);
void RS485_USART_IRQHandler(void);

#endif /* __RS485_H */
