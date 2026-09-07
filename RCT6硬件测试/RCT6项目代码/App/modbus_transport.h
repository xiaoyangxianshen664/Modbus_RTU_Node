#ifndef MODBUS_TRANSPORT_H
#define MODBUS_TRANSPORT_H

#include "stm32f1xx_hal.h"

extern TIM_HandleTypeDef g_modbus_timer;

void modbus_transport_init(void);
void modbus_transport_on_byte(void);
uint8_t modbus_transport_on_timer(void);
uint8_t modbus_transport_frame_ready(void);
uint8_t modbus_transport_frame_valid(void);
void modbus_transport_frame_clear(void);

#endif /* MODBUS_TRANSPORT_H */
