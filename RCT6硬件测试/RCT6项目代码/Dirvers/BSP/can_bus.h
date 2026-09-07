#ifndef __CAN_BUS_H
#define __CAN_BUS_H

#include "stm32f1xx_hal.h"
#include "rct6_pinmap.h"

#define CAN_BUS_EXTENDED_ID       0x1314U
#define CAN_BUS_DATA_LENGTH       8U
#define CAN_BUS_TX_TIMEOUT_MS     100U

extern CAN_HandleTypeDef g_can1;

HAL_StatusTypeDef CAN_Bus_Init(void);
HAL_StatusTypeDef CAN_Bus_Send(const uint8_t data[CAN_BUS_DATA_LENGTH]);
uint8_t CAN_Bus_Read(uint8_t data[CAN_BUS_DATA_LENGTH]);
void CAN_Bus_RX0_IRQHandler(void);

#endif /* __CAN_BUS_H */
