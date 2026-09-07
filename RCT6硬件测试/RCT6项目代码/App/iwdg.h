#ifndef PROJECT_IWDG_H
#define PROJECT_IWDG_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef Project_IWDG_Init(void);
void Project_IWDG_Feed(void);

#endif /* PROJECT_IWDG_H */
