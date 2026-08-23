/**
  * @file    bsp_485.c
  * @brief   RS485 回环测试 — USART2 + PB8 方向控制
  *
  * RS485 = USART2 (PD5=TX, PD6=RX, 115200) + PB8=DE/RE（高=发送, 低=接收）
  * 回环测试：用杜邦线短接 PD5 和 PD6，不经过 485 芯片
  */

#include "./485/bsp_485.h"

UART_HandleTypeDef bsp_485_huart;                                       /* USART2 句柄 */

volatile uint8_t  bsp_485_rx_buf[256];                                 /* 接收缓冲区 */
volatile uint16_t bsp_485_rx_len;                                       /* 接收字节数 */
volatile uint8_t  bsp_485_rx_flag;                                     /* 接收完成标志 */
volatile uint8_t  bsp_485_rx_byte;                                     /* 接收暂存变量（回调 + Init 共用） */

/* ════════════════════════════════════════════════════════════════
 * BSP_485_Init — 初始化 RS485（USART2 + PB8 方向脚，默认接收）
 *
 * 原型：void BSP_485_Init(void)
 * 返值：无
 *
 * 原理：
 *   ① 配 PD5(TX)/PD6(RX) 为 AF7（USART2）
 *   ② 配 PB8 为推挽输出，拉低 = 接收模式（开机默认听总线）
 *   ③ 配 USART2 波特率 115200 + RXNE 中断
 *
 * 调用示例：
 *   BSP_485_Init();  // main() 里调一次
 * ════════════════════════════════════════════════════════════════
 */
void BSP_485_Init(void)
{
    /* ① 配引脚 */
    BSP_485_GPIO_CLK_ENABLE();                                         /* 开 GPIOD 时钟 */
		BSP_485_RE_DE_CLK_ENABLE();                                        /* 开 GPIOB_8 时钟,多开一次不影响 */

    GPIO_InitTypeDef g = {0};
    g.Mode      = GPIO_MODE_AF_PP;                                     /* 复用推挽 */
    g.Pull      = GPIO_PULLUP;																				 
    g.Speed     = GPIO_SPEED_HIGH;
    g.Alternate = BSP_485_AF;                                          /* AF7 = USART2 */

    g.Pin = BSP_485_TX_PIN;                                            /* PD5 = TX */
    HAL_GPIO_Init(BSP_485_GPIO_PORT, &g);

    g.Pin = BSP_485_RX_PIN;                                            /* PD6 = RX */
    HAL_GPIO_Init(BSP_485_GPIO_PORT, &g);

    /* PB8 = DE/RE 方向脚，默认低 = 接收模式 */
    g.Mode      = GPIO_MODE_OUTPUT_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_HIGH;
    g.Pin       = BSP_485_RE_DE_PIN;                                  /* PB8 */
    HAL_GPIO_Init(BSP_485_RE_DE_PORT, &g);
    BSP_485_RX_EN();                                                    /* 方向切换宏：发送前拉高电平GPIOB_8 ，发完拉低电平GPIOB_8  */ 

    /* ② 配 USART2 */
    __USART2_CLK_ENABLE() ;                                            /* 开 USART2 时钟 */

    bsp_485_huart.Instance          = BSP_485_USART;                   /* USART2 */
    bsp_485_huart.Init.BaudRate     = BSP_485_BAUDRATE;               /* 9600 */
		
    /* 开启校验时，9B 才表示 8 位有效数据 + 1 位校验位 */
    bsp_485_huart.Init.WordLength   = UART_WORDLENGTH_9B;							/* 数据位1字节Byte */
    bsp_485_huart.Init.StopBits     = UART_STOPBITS_1;							  /*停止位1位*/
    bsp_485_huart.Init.Parity       = UART_PARITY_EVEN;								/*启用偶校验位（Parity Bit）*/
    bsp_485_huart.Init.Mode         = UART_MODE_TX_RX;								 /*发送和接收模式都开*/
    bsp_485_huart.Init.OverSampling = UART_OVERSAMPLING_16;							/*接收器在一个比特的时间里采样 16 次，取中间的 8、9、10 次采样值（或者多数表决）作为最终结果，数据才比较准*/		
    HAL_UART_Init(&bsp_485_huart);

    /* ③ 启动接收中断 */
    HAL_NVIC_SetPriority(BSP_485_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(BSP_485_IRQn);
    __HAL_UART_ENABLE_IT(&bsp_485_huart, UART_IT_RXNE);									//如果有数据来了，允许产生中断，此处为开阀门

    /* 启动首次接收 */
    HAL_UART_Receive_IT(&bsp_485_huart, (uint8_t *)&bsp_485_rx_byte, 1);
		
		//是“下达订单”。告诉 HAL 库：“我现在想要收 1 个字节，你帮我把这 1 个字节存到 bsp_485_rx_byte 这个地址里。”
}

/* ════════════════════════════════════════════════════════════════
 * BSP_485_SendByte — 发送 1 字节（方向自动切换）
 *
 * 原型：void BSP_485_SendByte(uint8_t ch)
 *       ch — [输入] 要发送的字节
 * 返值：无
 *
 * 调用示例：
 *   BSP_485_SendByte(0x55);
 * ════════════════════════════════════════════════════════════════
 */
void BSP_485_SendByte(uint8_t ch)
{
    BSP_485_TX_EN();                                                    /* PB8 拉高 = 发送模式 */
    for (volatile uint16_t d = 0; d < 1000; d++);                       /* 等 485 芯片切换 */
    HAL_UART_Transmit(&bsp_485_huart, &ch, 1, 100);
    while (__HAL_UART_GET_FLAG(&bsp_485_huart, UART_FLAG_TC) == RESET) { }                    /* 发 1 字节 */
    for (volatile uint16_t d = 0; d < 1000; d++);                       /* 等数据发完 */
    BSP_485_RX_EN();                                                    /* PB8 拉低 = 接收模式 */
}

/* ════════════════════════════════════════════════════════════════
 * BSP_485_SendBuf — 发送整段数据
 * ════════════════════════════════════════════════════════════════
 */
void BSP_485_SendBuf(uint8_t *buf, uint16_t len)
{
    BSP_485_TX_EN();																										/* PB8 拉高 = 发送模式 */
    for (volatile uint16_t d = 0; d < 1000; d++);												/* 等 485 芯片切换 */
    HAL_UART_Transmit(&bsp_485_huart, buf, len, 1000);
    while (__HAL_UART_GET_FLAG(&bsp_485_huart, UART_FLAG_TC) == RESET) { }									/* 发 缓存区 字节 */
    for (volatile uint16_t d = 0; d < 1000; d++);												/* 等数据发完 */
    BSP_485_RX_EN();																									  /* PB8 拉低 = 接收模式 */
}
