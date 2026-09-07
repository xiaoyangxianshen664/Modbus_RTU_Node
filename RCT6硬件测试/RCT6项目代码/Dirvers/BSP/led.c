#include "led.h"
#include "rct6_pinmap.h"

static GPIO_TypeDef *const g_led_ports[BSP_LED_COUNT] =
{
    RCT6_LED_RED_PORT,
    RCT6_LED_GREEN_PORT
};

static const uint16_t g_led_pins[BSP_LED_COUNT] =
{
    RCT6_LED_RED_PIN,
    RCT6_LED_GREEN_PIN
};

/**
 * @brief  初始化板载红灯和绿灯
 * @param  无
 * @return 无
 * @note   PB10、PB11 配置为推挽输出；硬件为低电平点亮，上电默认关闭。
 * @example LED_Init();
 */
void LED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    const uint16_t led_pins = RCT6_LED_RED_PIN | RCT6_LED_GREEN_PIN;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, led_pins, RCT6_LED_OFF_STATE);  // 先写关闭电平，避免切换为输出时闪灯

    gpio.Pin = led_pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

/**
 * @brief  点亮指定板载 LED
 * @param  led [输入] BSP_LED_RED 或 BSP_LED_GREEN
 * @return 无
 * @note   LED 阳极经限流电阻接 3V3，GPIO 输出低电平时灌电流点亮。
 * @example LED_On(BSP_LED_RED);
 */
void LED_On(BSP_LED_TypeDef led)
{
    if (led < BSP_LED_COUNT)
    {
        HAL_GPIO_WritePin(g_led_ports[led], g_led_pins[led], RCT6_LED_ON_STATE);
    }
}

/**
 * @brief  熄灭指定板载 LED
 * @param  led [输入] BSP_LED_RED 或 BSP_LED_GREEN
 * @return 无
 * @note   GPIO 输出高电平后，LED 两端没有足够压差，因此熄灭。
 * @example LED_Off(BSP_LED_GREEN);
 */
void LED_Off(BSP_LED_TypeDef led)
{
    if (led < BSP_LED_COUNT)
    {
        HAL_GPIO_WritePin(g_led_ports[led], g_led_pins[led], RCT6_LED_OFF_STATE);
    }
}

/**
 * @brief  翻转指定板载 LED 的亮灭状态
 * @param  led [输入] BSP_LED_RED 或 BSP_LED_GREEN
 * @return 无
 * @note   直接翻转 GPIO 输出寄存器，适合周期闪烁测试。
 * @example LED_Toggle(BSP_LED_RED);
 */
void LED_Toggle(BSP_LED_TypeDef led)
{
    if (led < BSP_LED_COUNT)
    {
        HAL_GPIO_TogglePin(g_led_ports[led], g_led_pins[led]);
    }
}
