/** 
  ******************************************************************************
  * @file    Templates_LL/Src/stm32F1xx_it.c
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides temp1late for all exceptions handler and
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_it.h"
#include "usart1.h"
#include "w25q256.h"
#include "sd_card.h"
#include "adc_multi.h"
#include "eeprom.h"
#include "rs485.h"
#include "can_bus.h"
#include "rtc_test.h"
#include "modbus_transport.h"
#include "project_app.h"

/** @addtogroup STM32F1xx_LL_Examples
  * @{
  */

/** @addtogroup Templates_LL
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/******************************************************************************/
/*                 STM32F1xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f1xx.s).                                               */
/******************************************************************************/

/**
  * @brief  处理 USART1 全局中断，包括 IDLE 帧结束事件
  * @param  无
  * @retval 无
  */
void USART1_IRQHandler(void)
{
  USART1_BSP_IRQHandler();
}

/**
  * @brief  处理 USART1 TX 对应的 DMA1 Channel4 中断
  * @param  无
  * @retval 无
  */
void DMA1_Channel4_IRQHandler(void)
{
  USART1_TX_DMA_IRQHandler();
}

/**
  * @brief  处理 USART1 RX 对应的 DMA1 Channel5 中断
  * @param  无
  * @retval 无
  */
void DMA1_Channel5_IRQHandler(void)
{
  USART1_RX_DMA_IRQHandler();
}

/**
  * @brief  处理 SPI1 RX 对应的 DMA1 Channel2 中断
  * @param  无
  * @retval 无
  */
void DMA1_Channel2_IRQHandler(void)
{
  W25Q256_RX_DMA_IRQHandler();
}

/**
  * @brief  处理 SPI1 TX 对应的 DMA1 Channel3 中断
  * @param  无
  * @retval 无
  */
void DMA1_Channel3_IRQHandler(void)
{
  W25Q256_TX_DMA_IRQHandler();
}



/**
  * @}
  */

/**
  * @}
  */

/**
  * @brief  处理SDIO全局中断, 包括数据结束和总线错误
  * @param  无
  * @retval 无
  */
void SDIO_IRQHandler(void)
{
  SD_Card_SDIO_IRQHandler();
}

/**
  * @brief  处理SDIO固定使用的DMA2 Channel4共享中断
  * @param  无
  * @retval 无
  */
void DMA2_Channel4_5_IRQHandler(void)
{
  SD_Card_DMA_IRQHandler();
}

/**
  * @brief  处理ADC1固定使用的DMA1 Channel1中断
  * @param  无
  * @retval 无
  */
void DMA1_Channel1_IRQHandler(void)
{
  ADC_Multi_DMA_IRQHandler();
}

/** @brief 处理I2C1事件中断, 推进EEPROM DMA状态机; @param 无; @retval 无 */
void I2C1_EV_IRQHandler(void)
{
  EEPROM_I2C_EV_IRQHandler();
}

/** @brief 处理I2C1错误中断; @param 无; @retval 无 */
void I2C1_ER_IRQHandler(void)
{
  EEPROM_I2C_ER_IRQHandler();
}

/** @brief 处理I2C1 TX固定使用的DMA1 Channel6中断; @param 无; @retval 无 */
void DMA1_Channel6_IRQHandler(void)
{
  EEPROM_TX_DMA_IRQHandler();
}

/** @brief 处理I2C1 RX固定使用的DMA1 Channel7中断; @param 无; @retval 无 */
void DMA1_Channel7_IRQHandler(void)
{
  EEPROM_RX_DMA_IRQHandler();
}

/** @brief 处理USART2的RS485接收与空闲帧中断; @param 无; @retval 无 */
void USART2_IRQHandler(void)
{
  RS485_USART_IRQHandler();
}

/** @brief TIM4每1ms检查一次Modbus RTU静默时间。 */
void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&g_modbus_timer);
}

/** @brief TIM4达到T3.5时通知Modbus任务处理完整请求帧。 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
  if ((timer != NULL) && (timer->Instance == TIM4) &&
      (modbus_transport_on_timer() != 0U))
  {
    project_modbus_notify_from_isr();
  }
}

/** @brief 处理CAN1 FIFO0与USB低优先级共享中断; @param 无; @retval 无 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
  CAN_Bus_RX0_IRQHandler();
}

/** @brief 处理RTC闹钟经EXTI17触发的中断; @param 无; @retval 无 */
void RTC_Alarm_IRQHandler(void)
{
  RTC_Test_AlarmIRQHandler();
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
