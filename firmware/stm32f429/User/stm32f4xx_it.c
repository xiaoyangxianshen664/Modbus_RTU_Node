#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "./SysTick/SysTick.h"
#include "./TIM7/TIM7.h"
#include "./Usart/Usart.h"
#include <stdio.h>
#include "task.h"

extern void xPortSysTickHandler(void);
extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_adc;

/******************************************************************************/
/*           Cortex-M4 系统异常处理                                           */
/******************************************************************************/

/**
 * @brief  SysTick 中断 — FreeRTOS 调度 + TimingDelay 延时
 */
void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();      /* FreeRTOS: 任务调度、vTaskDelay 时基 */
    }
}

/**
 * @brief  HardFault —— 调试时在这里打断点，快速定位问题
 */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}

/******************************************************************************/
/* 其余系统异常（NMI/MemManage/BusFault/UsageFault/SVC/PendSV/...）           */
/* 启动文件中已有 WEAK 默认实现（B . 死循环），不需要在此重复。              */
/* 用到外设中断时，在此文件中追加对应的 xxx_IRQHandler 即可。                */
/******************************************************************************/

/******************************************************************************/
/*                          GPIO 外部中断服务函数                              */
/******************************************************************************/

#include "./LED/LED.h"
#include "./Key/Key.h"


/**
 * @brief  KEY1 (PA0) 外部中断 —— 红灯翻转
 */
void EXTI0_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) != RESET)
    {
        /* 软件消抖（ISR 内不能用 HAL_Delay） */
        for (volatile uint32_t i = 0; i < 200000; i++)
            ;

        if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) == RESET)
            return; // 抖动，直接退出

        if (HAL_GPIO_ReadPin(LED_R_GPIO_PORT, LED_R_PIN) == GPIO_PIN_RESET)
            LED_R(1); // 亮 → 灭
        else
            LED_R(0); // 灭 → 亮

        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0); // 干完活再清标志
    }
}

/**
 * @brief  KEY2 (PC13) 外部中断 — 恢复 task1
 * @note   优先级设为 5（≥5 可安全调 FreeRTOS API）
 */
void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) != RESET)
    {
        /* 软件消抖（ISR 内不能用 HAL_Delay） */
        for (volatile uint32_t i = 0; i < 200000; i++)
            ;

        if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) == RESET)
            return;

        /* 翻转绿灯（消抖后确认不是干扰） */
        if (HAL_GPIO_ReadPin(LED_G_GPIO_PORT, LED_G_PIN) == GPIO_PIN_RESET)
            LED_G(1);
        else
            LED_G(0);

        /* ── 中断里恢复 task1 ──
         * ISR 必须用 FromISR 版本，vTaskResume 只能任务里调！
         * pdTRUE = 真正执行了恢复（非抖动），才通知 task1 打印一次 */


        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_13);
    }
}

/******************************************************************************/
/*                          UART 串口中断服务函数                               */
/******************************************************************************/

/* 接收缓冲区（extern 自 BSP/Usart/Usart.c） */
extern uint8_t usart1_rx_buf[50];
extern uint8_t usart1_rx_len;
extern volatile uint8_t usart1_rx_flag;

extern uint8_t dma_rx_buf[128];
extern volatile uint8_t dma_rx_flag;
extern uint16_t dma_rx_len;

extern UART_HandleTypeDef huart1;

/**
 * @brief  USART1 中断 —— RXNE + IDLE
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1); // 清硬件 IDLE 标志

        /* 判断 DMA 模式还是 IT 模式 */
        uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx); // 读 DMA 缓存区剩余计数值
        if (ndtr < DMA_RX_BUF_SIZE)                             // 缓存区128字节
        {
            /* DMA 模式 */
            dma_rx_len = DMA_RX_BUF_SIZE - ndtr; // 实际收到 的字节长度
            if (dma_rx_len > 0)                  /* 真的收到数据才通知 main */
            {
                dma_rx_flag = 1;
            }
            HAL_UART_DMAStop(&huart1);                                  // 停掉 DMA
            HAL_UART_Receive_DMA(&huart1, dma_rx_buf, DMA_RX_BUF_SIZE); // 重启，准备收下一帧
            __HAL_UART_CLEAR_IDLEFLAG(&huart1);                         /* 重启 DMA 后清掉可能立即触发的 IDLE */
        }
        else
        {
            /* IT 模式 */
            usart1_rx_flag = 1;
        }
    }
}

/* ── DMA 中断服务 ── */
void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

void DMA2_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}

/* ADC1 使用 DMA2 Stream0；HAL 在这里分发半传输/全传输回调。 */
void DMA2_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc);
}

/******************************************************************************/
/*                          TIM 定时器中断服务函数                              */
/******************************************************************************/




/* ── TIM6 运行时间统计时基 —— FreeRTOSRunTimeTicks++ ── */
extern TIM_HandleTypeDef htim6;

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

/* ── TIM7 HAL 时基中断 —— HAL_IncTick ── */
extern TIM_HandleTypeDef htim7;

void TIM7_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim7);
}

/******************************************************************************/
/*                          USART2 (RS485) 中断服务函数                         */
/******************************************************************************/

/* 485 句柄（定义在 BSP/485/bsp_485.c；接收回调在 BSP/Usart/Usart.c） */
extern UART_HandleTypeDef bsp_485_huart;

/**
 * @brief  USART2 中断 —— 485 收字节入口
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&bsp_485_huart);       /* 交给 HAL 处理 RXNE */
}

/******************************************************************************/
/*                          TIM4 (T3.5 静默定时器) 中断                        */
/******************************************************************************/

/* TIM4 句柄（定义在 App/modbus_transport.c） */
extern TIM_HandleTypeDef htim4;

/**
 * @brief  TIM4 中断 —— T3.5 静默定时器溢出（一帧收完）
 */
void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim4);                /* 交给 HAL，最终调 PeriodElapsedCallback */
}
