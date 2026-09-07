#include "key.h"
#include "rct6_pinmap.h"

static GPIO_TypeDef *const g_key_ports[BSP_KEY_COUNT] =
{
    RCT6_KEY1_PORT,
    RCT6_KEY2_PORT
};

static const uint16_t g_key_pins[BSP_KEY_COUNT] =
{
    RCT6_KEY1_PIN,
    RCT6_KEY2_PIN
};

static const GPIO_PinState g_key_pressed_states[BSP_KEY_COUNT] =
{
    RCT6_KEY1_PRESSED_STATE,
    RCT6_KEY2_PRESSED_STATE
};

/**
 * @brief  初始化 KEY1 和 KEY2 的 GPIO 输入
 * @param  无
 * @return 无
 * @note   KEY1=PA0、KEY2=PC13；按下接入高电平，未按下由内部下拉保持低电平。
 * @example KEY_Init();
 */
void KEY_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = RCT6_KEY1_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RCT6_KEY1_PORT, &gpio);

    gpio.Pin = RCT6_KEY2_PIN;
    HAL_GPIO_Init(RCT6_KEY2_PORT, &gpio);
}

/**
 * @brief  读取指定按键当前是否按下
 * @param  key [输入] BSP_KEY_1 或 BSP_KEY_2
 * @return 1 表示按下，0 表示松开或按键编号无效
 * @note   本函数返回原始电平状态，不包含软件消抖；测试流程可延时约 20ms 后再次确认。
 * @example if (KEY_IsPressed(BSP_KEY_1) != 0U) { LED_On(BSP_LED_RED); }
 */
uint8_t KEY_IsPressed(BSP_KEY_TypeDef key)
{
    if (key >= BSP_KEY_COUNT)
    {
        return 0U;
    }

    return (uint8_t)(HAL_GPIO_ReadPin(g_key_ports[key], g_key_pins[key]) ==
                     g_key_pressed_states[key]);
}
