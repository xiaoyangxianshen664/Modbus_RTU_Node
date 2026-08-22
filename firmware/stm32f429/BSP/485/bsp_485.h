/**
  * @file    bsp_485.h
  * @brief   RS485 驱动头文件 — USART2 + PB8 方向控制
  *
  * RS485 = USART2（PD5=TX, PD6=RX, 9600）+ PB8=DE/RE（高=发送, 低=接收）
  * 半双工通信：发数据前拉高 PB8，发完拉低切回接收。
  */

#ifndef __BSP_485_H
#define __BSP_485_H

#include "stm32f4xx.h"
#include <stdio.h>

/* ── USART2 串口配置 ── */
#define BSP_485_USART           USART2                                 /* 485 用 USART2 */
#define BSP_485_CLK_ENABLE()    __USART2_CLK_ENABLE()                  /* USART2 时钟 */
#define BSP_485_BAUDRATE        9600                                 /* 波特率 */

/* ── USART2 引脚：PD5=TX, PD6=RX, AF7 ── */
#define BSP_485_TX_PIN          GPIO_PIN_5                             /* PD5 = TX */
#define BSP_485_RX_PIN          GPIO_PIN_6                             /* PD6 = RX */
#define BSP_485_GPIO_PORT       GPIOD                                  /* PD 端口 */
#define BSP_485_GPIO_CLK_ENABLE() __GPIOD_CLK_ENABLE()                 /* GPIOD 时钟 */
#define BSP_485_AF              GPIO_AF7_USART2                        /* AF7 = USART2 */
#define BSP_485_IRQn            USART2_IRQn                            /* USART2 中断号 */

/* ── 方向控制：PB8 = DE/RE（高=发送, 低=接收） ── */
#define BSP_485_RE_DE_PIN          GPIO_PIN_8                          /* PB8 = DE/RE 引脚 */
#define BSP_485_RE_DE_PORT         GPIOB                               /* PB 端口 */
#define BSP_485_RE_DE_CLK_ENABLE() __GPIOB_CLK_ENABLE()                /* GPIOB 时钟 */

/* 方向切换宏：发前拉高，发完拉低 */
#define BSP_485_TX_EN()  HAL_GPIO_WritePin(BSP_485_RE_DE_PORT, BSP_485_RE_DE_PIN, GPIO_PIN_SET)    /* 发送模式 */
#define BSP_485_RX_EN()  HAL_GPIO_WritePin(BSP_485_RE_DE_PORT, BSP_485_RE_DE_PIN, GPIO_PIN_RESET)  /* 接收模式 */

/* ── 调试宏：打印字节数组 ── */
#define BSP_485_DEBUG_ARRAY(a,n) do{ for(int _i=0;_i<(n);_i++) printf("%02x ",((uint8_t*)(a))[_i]); printf("\r\n"); }while(0)

/* ── 外部变量（bsp_485.c 中定义） ── */
extern UART_HandleTypeDef bsp_485_huart;                               /* USART2 句柄 */
extern volatile uint8_t  bsp_485_rx_buf[256];                          /* 接收缓冲区 */
extern volatile uint16_t bsp_485_rx_len;                               /* 已接收字节数 */
extern volatile uint8_t  bsp_485_rx_flag;                              /* 收满标志（256字节） */
extern volatile uint8_t  bsp_485_rx_byte;                              /* 单字节接收暂存 */

/* ── API ── */
void BSP_485_Init(void);                                               /* 初始化 RS485（默认接收模式） */
void BSP_485_SendByte(uint8_t ch);                                     /* 发送 1 字节（自动切换方向） */
void BSP_485_SendBuf(uint8_t *buf, uint16_t len);                      /* 发送整段数据 */

#endif
