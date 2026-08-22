#include "./Key/Key.h"
#include "./SysTick/SysTick.h"

void Key_Init(void)
{
    KEY1_GPIO_CLK_ENABLE();
    KEY2_GPIO_CLK_ENABLE();

    GPIO_InitTypeDef Key_InitStruct = {0};
    Key_InitStruct.Mode = GPIO_MODE_INPUT;
    Key_InitStruct.Pull = GPIO_NOPULL; /* 板子有外部下拉电阻 */
    Key_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    Key_InitStruct.Pin = KEY1_PIN;
    HAL_GPIO_Init(KEY1_GPIO_PORT, &Key_InitStruct);

    Key_InitStruct.Pin = KEY2_PIN;
    HAL_GPIO_Init(KEY2_GPIO_PORT, &Key_InitStruct);
}

/**
 * @brief  非阻塞按键扫描（轮询消抖版）
 * @note   delay_ms 读 SysTick->VAL 做轮询消抖，不依赖 RTOS 延时
 */
uint8_t Key_Scan(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    if (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_ON)
    {
        delay_ms(10);  /* 消抖 10ms */
        if (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_ON)
        {
            while (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_ON)
                ;  /* 等待松开 */
            return KEY_ON;
        }
    }
    return KEY_OFF;
}
