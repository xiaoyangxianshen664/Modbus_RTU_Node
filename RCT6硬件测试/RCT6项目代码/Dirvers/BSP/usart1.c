#include "usart1.h"
#include "rct6_pinmap.h"
#include "sys.h"
#include <string.h>

UART_HandleTypeDef g_usart1_uart;
DMA_HandleTypeDef g_usart1_tx_dma;
DMA_HandleTypeDef g_usart1_rx_dma;

static uint8_t g_usart1_rx_dma_buffer[USART1_RX_DMA_BUFFER_SIZE];
static uint8_t g_usart1_rx_frame[USART1_RX_DMA_BUFFER_SIZE];
static uint8_t g_usart1_tx_dma_buffer[USART1_TX_DMA_BUFFER_SIZE];
static volatile uint16_t g_usart1_rx_frame_length;
static volatile uint8_t g_usart1_rx_frame_ready;

/**
 * @brief  将 DMA 已接收的一帧保存到稳定缓冲区
 * @param  length [输入] DMA 缓冲区内的有效字节数
 * @return 无
 * @note   主循环尚未读取上一帧时丢弃新帧，防止覆盖正在处理的数据。
 * @example USART1_PublishFrame(5U);
 */
static void USART1_PublishFrame(uint16_t length)
{
    if ((length == 0U) || (length > USART1_RX_DMA_BUFFER_SIZE) ||
        (g_usart1_rx_frame_ready != 0U))
    {
        return;
    }

    memcpy(g_usart1_rx_frame, g_usart1_rx_dma_buffer, length);
    g_usart1_rx_frame_length = length;
    g_usart1_rx_frame_ready = 1U;
}

/**
 * @brief  重新启动 USART1 DMA 接收
 * @param  无
 * @return 无
 * @note   普通 DMA 每次判定帧结束后都必须重新装载地址和计数值。
 * @example USART1_RestartRxDMA();
 */
static void USART1_RestartRxDMA(void)
{
    (void)HAL_UART_Receive_DMA(&g_usart1_uart,
                               g_usart1_rx_dma_buffer,
                               USART1_RX_DMA_BUFFER_SIZE);
    __HAL_UART_CLEAR_IDLEFLAG(&g_usart1_uart);
}

/**
 * @brief  初始化 USART1、收发 DMA 和空闲中断
 * @param  baudrate [输入] 串口波特率，例如 115200
 * @return 无
 * @note   STM32F103 固定映射：PA9=TX、PA10=RX、DMA1_Channel4=TX、Channel5=RX。
 * @example USART1_BSP_Init(115200U);
 */
void USART1_BSP_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio.Pin = RCT6_DEBUG_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RCT6_DEBUG_TX_PORT, &gpio);

    gpio.Pin = RCT6_DEBUG_RX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RCT6_DEBUG_RX_PORT, &gpio);

    g_usart1_uart.Instance = USART1;
    g_usart1_uart.Init.BaudRate = baudrate;
    g_usart1_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_usart1_uart.Init.StopBits = UART_STOPBITS_1;
    g_usart1_uart.Init.Parity = UART_PARITY_NONE;
    g_usart1_uart.Init.Mode = UART_MODE_TX_RX;
    g_usart1_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_usart1_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_usart1_uart) != HAL_OK)
    {
        Error_Handler();
    }

    g_usart1_tx_dma.Instance = DMA1_Channel4;
    g_usart1_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    g_usart1_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_usart1_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_usart1_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_usart1_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_usart1_tx_dma.Init.Mode = DMA_NORMAL;
    g_usart1_tx_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&g_usart1_tx_dma) != HAL_OK)
    {
        Error_Handler();
    }

    g_usart1_rx_dma.Instance = DMA1_Channel5;
    g_usart1_rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_usart1_rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_usart1_rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_usart1_rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_usart1_rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_usart1_rx_dma.Init.Mode = DMA_NORMAL;
    g_usart1_rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&g_usart1_rx_dma) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&g_usart1_uart, hdmatx, g_usart1_tx_dma);
    __HAL_LINKDMA(&g_usart1_uart, hdmarx, g_usart1_rx_dma);

    HAL_NVIC_SetPriority(USART1_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    __HAL_UART_CLEAR_IDLEFLAG(&g_usart1_uart);
    __HAL_UART_ENABLE_IT(&g_usart1_uart, UART_IT_IDLE);
    USART1_RestartRxDMA();
}

/**
 * @brief  使用 DMA 发送一段 USART1 数据
 * @param  data [输入] 待发送数据首地址
 * @param  length [输入] 待发送字节数，最大 200 字节
 * @return HAL_OK 表示已启动；HAL_BUSY 表示上次发送未完成；HAL_ERROR 表示参数无效
 * @note   函数先复制到内部缓冲区，调用者返回后可以立即复用原数据。
 * @example USART1_SendDMA((const uint8_t *)"OK\r\n", 4U);
 */
HAL_StatusTypeDef USART1_SendDMA(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) || (length > USART1_TX_DMA_BUFFER_SIZE))
    {
        return HAL_ERROR;
    }

    if (g_usart1_uart.gState != HAL_UART_STATE_READY)
    {
        return HAL_BUSY;
    }

    memcpy(g_usart1_tx_dma_buffer, data, length);
    return HAL_UART_Transmit_DMA(&g_usart1_uart, g_usart1_tx_dma_buffer, length);
}

/**
 * @brief  从中断接收区取出一帧 USART1 数据
 * @param  destination [输出] 用户接收缓冲区
 * @param  capacity [输入] 用户缓冲区容量
 * @return 实际复制的字节数；0 表示当前没有完整帧或参数无效
 * @note   复制期间短暂关闭中断，避免 ISR 与主循环同时访问帧数据。
 * @example length = USART1_ReadFrame(buffer, sizeof(buffer));
 */
uint16_t USART1_ReadFrame(uint8_t *destination, uint16_t capacity)
{
    uint16_t length;
    uint32_t primask;

    if ((destination == NULL) || (capacity == 0U) || (g_usart1_rx_frame_ready == 0U))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    length = g_usart1_rx_frame_length;
    if (length > capacity)
    {
        length = capacity;
    }
    memcpy(destination, g_usart1_rx_frame, length);
    g_usart1_rx_frame_ready = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return length;
}

/**
 * @brief  处理 USART1 全局中断
 * @param  无
 * @return 无
 * @note   IDLE 表示总线空闲一个字符时间，用 DMA 剩余计数计算本帧长度。
 * @example 由 USART1_IRQHandler() 调用。
 */
void USART1_BSP_IRQHandler(void)
{
    if ((__HAL_UART_GET_FLAG(&g_usart1_uart, UART_FLAG_IDLE) != RESET) &&
        (__HAL_UART_GET_IT_SOURCE(&g_usart1_uart, UART_IT_IDLE) != RESET))
    {
        uint16_t remaining;
        uint16_t received;

        __HAL_UART_CLEAR_IDLEFLAG(&g_usart1_uart);
        remaining = (uint16_t)__HAL_DMA_GET_COUNTER(&g_usart1_rx_dma);
        (void)HAL_UART_DMAStop(&g_usart1_uart);
        received = USART1_RX_DMA_BUFFER_SIZE - remaining;
        USART1_PublishFrame(received);
        USART1_RestartRxDMA();
    }

    HAL_UART_IRQHandler(&g_usart1_uart);
}

/**
 * @brief  处理 USART1 TX DMA 中断
 * @param  无
 * @return 无
 * @note   交给 HAL 更新 DMA 与 UART 发送状态。
 * @example 由 DMA1_Channel4_IRQHandler() 调用。
 */
void USART1_TX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_usart1_tx_dma);
}

/**
 * @brief  处理 USART1 RX DMA 中断
 * @param  无
 * @return 无
 * @note   缓冲区收满时 HAL 将调用 HAL_UART_RxCpltCallback()。
 * @example 由 DMA1_Channel5_IRQHandler() 调用。
 */
void USART1_RX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_usart1_rx_dma);
}

/**
 * @brief  HAL 串口 DMA 接收完成回调
 * @param  huart [输入] 触发回调的串口句柄
 * @return 无
 * @note   USART1 缓冲区恰好收满时保存 128 字节并重新启动 DMA。
 * @example 由 HAL_DMA_IRQHandler() 间接调用。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        USART1_PublishFrame(USART1_RX_DMA_BUFFER_SIZE);
        USART1_RestartRxDMA();
    }
}

/**
 * @brief  HAL 串口错误回调
 * @param  huart [输入] 发生错误的串口句柄
 * @return 无
 * @note   USART1 发生溢出、噪声或帧错误后重新启动 DMA 接收。
 * @example 由 HAL_UART_IRQHandler() 间接调用。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        (void)HAL_UART_DMAStop(&g_usart1_uart);
        USART1_RestartRxDMA();
    }
}
