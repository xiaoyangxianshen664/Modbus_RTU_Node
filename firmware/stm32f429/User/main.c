#include "stm32f4xx_hal.h"
#include "./LED/LED.h"
#include "./Key/Key.h"
#include "./Usart/Usart.h"
#include "./TIM7/TIM7.h"
#include "./SysTick/SysTick.h"
#include "./485/bsp_485.h"
#include "modbus_transport.h"
#include "freertos_demo.h"
#include "./ADC/ADC_Multi.h"
#include <stdio.h>

/* ════════════════════════════════════════════════════════════
 * main — 阶段 3 FreeRTOS Modbus RTU 从站入口
 *
 * 原型：int main(void)
 * 形参：无
 * 返值：正常情况下不会返回
 *
 * 数据流：USART2 中断收字节 → TIM4 判断帧边界并通知 ModbusTask
 *         → ModbusTask 复制完整帧 → 纯 C 协议层解析 → RS485 响应
 *
 * 调用示例：
 *   main();  // 复位后由启动文件调用
 * ════════════════════════════════════════════════════════════ */
int main(void)
{
  HAL_Init();                                         // 初始化 HAL、NVIC 分组和默认 SysTick
  SysTick_Init();                                     // 配置 HSE 25MHz → 系统时钟 180MHz
  TIM7_Init();                                        // TIM7 提供 HAL 的 1ms 时基，避免与 FreeRTOS SysTick 冲突
  LED_Init();                                         // 初始化板载状态灯
  Key_Init();                                         // 初始化按键（保留供后续调试使用）

  Usart1_Init(115200);                                // USART1：调试串口，115200 8N1，供 printf 使用
  Usart1_DMA_Init();                                  // 初始化 USART1 收发 DMA
  HAL_UART_AbortReceive(&huart1);                     // 终止 USART1 初始化时启动的单字节 IT 接收
  HAL_UART_Receive_DMA(&huart1, dma_rx_buf, DMA_RX_BUF_SIZE); // 改用 DMA 接收 USART1 调试数据
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);                 // 清除切换接收模式时可能残留的 IDLE 标志

  BSP_485_Init();                                     // USART2 + PB8 DE/RE，9600 8E1，默认进入接收模式
  modbus_transport_init();                            // TIM4 计量 T1.5/T3.5，判断帧内超时和帧结束
  ADC_Multi_Init();                                   // 阶段 4：启动 PC3 + PA4 两路 ADC DMA 扫描

  printf("Modbus RTU Node - ADC DMA stage 4!\r\n");  // 调度器启动前从 USART1 输出一次启动信息

  freertos_demo();                                    // 创建四个业务任务并启动 FreeRTOS 调度器

  while (1)
  {
    // 调度器正常启动后不会返回；若堆不足导致启动失败，则停在这里便于调试。
  }
}
