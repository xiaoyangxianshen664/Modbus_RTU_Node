#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "./LED/LED.h"
#include "./Key/Key.h"
#include "./Usart/Usart.h"
#include "./TIM7/TIM7.h"
#include "./SysTick/SysTick.h"
#include "./485/bsp_485.h"
#include "modbus_transport.h"
#include "freertos_demo.h"
#include <string.h>
#include <stdio.h>
#include "modbus_rtu.h"
#include "modbus_crc16.h"
#include "modbus_registers.h"

static modbus_registers_t g_registers;                 // 创建结构体变量，作为寄存器地址映射表
static uint8_t g_request[MODBUS_MAX_ADU_SIZE];         // 完整请求帧副本，交给纯 C 协议层解析
static uint8_t g_response[MODBUS_MAX_ADU_SIZE];        // 协议层生成的完整响应帧，最后交给 RS485 发送

/* ════════════════════════════════════════════════════════════
 * main — 阶段 2 裸机 Modbus RTU 从站入口
 *
 * 数据流：USART2 中断收字节 → TIM4 判断帧边界 → 主循环复制完整帧
 *         → protocol 纯 C 协议层解析 → USART2/RS485 发送响应
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
  modbus_registers_init(&g_registers);                // 清零寄存器，并把 holding[0] 初始化为 1000

  printf("Modbus RTU Node - protocol test!\r\n");   // 仅从 USART1 输出启动信息

	
  /* 阶段 2 采用裸机主循环处理完整帧；阶段 3 再改为 ModbusTask。 */
  // freertos_demo();

  while (1)
  {
    /* TIM4 检测到总线静默超过 T3.5，说明接收缓冲区中已有一帧待处理。 */
    if (modbus_transport_frame_ready())
    {
      uint16_t request_length;                       // 本次请求帧的实际字节数
      uint8_t request_valid;                         // T1.5 检查结果：1=帧内连续，0=帧内曾超时
      size_t response_length = 0U;                   // 协议层实际生成的响应字节数

      /* USART2 中断也会访问接收缓冲区，因此复制和清状态期间短暂关中断。 */
			
      __disable_irq();                               // 进入短临界区，防止复制时又收到新字节
			
      request_length = bsp_485_rx_len;               // 保存当前帧长度
      if (request_length > MODBUS_MAX_ADU_SIZE)
          request_length = MODBUS_MAX_ADU_SIZE;      // 二次保护：协议 ADU 最大 256 字节
      memcpy(g_request, (const void *)bsp_485_rx_buf, request_length); // 复制到协议层专用请求缓冲区
			
			/*清状态*/
      request_valid = modbus_transport_frame_valid();// 清状态前先保存 T1.5 检查结果
      bsp_485_rx_len = 0U;                           // 释放 RS485 接收缓冲区，准备接收下一帧
      modbus_transport_frame_clear();                // 清 frame_ready，并为下一帧恢复初始状态
			
      __enable_irq();                                // 退出临界区，恢复 USART2/TIM4 中断

      /* 只有 T1.5 合格的帧才进入协议层；协议层继续检查长度、CRC、地址和功能码。 */
      if (request_valid &&
          modbus_rtu_process_request(g_request, request_length, &g_registers,
                                     g_response, sizeof(g_response),
                                     &response_length) == MODBUS_RTU_RESPONSE_READY)
      {
          BSP_485_SendBuf(g_response, (uint16_t)response_length); // PB8 切发送，等待 TC 后切回接收
      }
    }
  }
}
