#include "iwdg.h"

static IWDG_HandleTypeDef g_iwdg;

/** 使用LSI、256分频和625重装值，建立约4秒独立看门狗。 */
HAL_StatusTypeDef Project_IWDG_Init(void)
{
    g_iwdg.Instance = IWDG;
    g_iwdg.Init.Prescaler = IWDG_PRESCALER_256;
    g_iwdg.Init.Reload = 625U;
    return HAL_IWDG_Init(&g_iwdg);
}

void Project_IWDG_Feed(void)
{
    (void)HAL_IWDG_Refresh(&g_iwdg);
}
