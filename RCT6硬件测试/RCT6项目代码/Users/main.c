#include "main.h"
#include "project_app.h"
#include "modbus_transport.h"
#include "config_storage.h"
#include "iwdg.h"

/**
 * @brief STM32F103RCT6版Modbus RTU Node项目入口。
 * @note  所有外设先使用已经逐项上板验证过的RCT6 BSP初始化，随后启动四任务FreeRTOS应用。
 */
int main(void)
{
    uint8_t rtc_retained = 0U;
    RTC_TimeTypeDef alarm_time = {0};

    bsp_Init();
    project_diagnostics_init();
    LED_Off(BSP_LED_RED);
    LED_Off(BSP_LED_GREEN);

    if (RS485_Init() != HAL_OK)
    {
        myprintf("RS485 init FAIL\r\n");
        Error_Handler();
    }
    modbus_transport_init();

    if (ADC_Multi_Init() != HAL_OK)
    {
        myprintf("ADC DMA init FAIL\r\n");
        Error_Handler();
    }

    if (RTC_Test_Init(&rtc_retained, &alarm_time) != HAL_OK)
    {
        myprintf("RTC init FAIL, logging time will be zero\r\n");
    }
    project_log_storage_mount();

    config_storage_init();
    myprintf("W25Q256 config storage %s\r\n",
             (config_storage_is_ready() != 0U) ? "PASS" : "FAIL");

    if (Project_IWDG_Init() != HAL_OK)
    {
        myprintf("IWDG init FAIL\r\n");
        Error_Handler();
    }

    myprintf("\r\nSTM32F103RCT6 Modbus RTU Node start\r\n");
    myprintf("USART2: 9600, 8E1, slave address=1\r\n");
    myprintf("FreeRTOS: Modbus + Monitor + Acquire + Log\r\n");
    project_app_start();

    while (1)
    {
    }
}
