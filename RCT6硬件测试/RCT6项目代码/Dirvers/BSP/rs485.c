#include "rs485.h"
#include <string.h>
#include "modbus_transport.h"

UART_HandleTypeDef g_rs485_uart;

static uint8_t g_rs485_receiving[RS485_RX_BUFFER_SIZE];
static uint8_t g_rs485_frame[RS485_RX_BUFFER_SIZE];
static volatile uint16_t g_rs485_receiving_length;
static volatile uint16_t g_rs485_frame_length;
static volatile uint8_t g_rs485_frame_ready;

/**
 * @brief  将RS485收件缓冲区发布为一帧完整数据
 * @param  无
 * @return 无
 * @note   USART2检测到IDLE后调用; 上一帧未取走时保留旧帧, 避免主循环读取中被覆盖.
 * @example 由RS485_USART_IRQHandler()内部调用.
 */
static void RS485_PublishFrame(void)
{
    uint16_t length = g_rs485_receiving_length;

    if ((length > 0U) && (g_rs485_frame_ready == 0U))
    {
        memcpy(g_rs485_frame, g_rs485_receiving, length);
        g_rs485_frame_length = length;
        g_rs485_frame_ready = 1U;
    }
    g_rs485_receiving_length = 0U;
}

/**
 * @brief  初始化RCT6板RS485接口
 * @param  无
 * @return HAL_OK表示USART2初始化成功; HAL_ERROR表示失败
 * @note   PA2=USART2_TX, PA3=USART2_RX, PB8同时控制DE和RE; 低电平为默认接收模式.
 * @example if (RS485_Init() != HAL_OK) { Error_Handler(); }
 */
HAL_StatusTypeDef RS485_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = RCT6_RS485_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RCT6_RS485_TX_PORT, &gpio);

    gpio.Pin = RCT6_RS485_RX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RCT6_RS485_RX_PORT, &gpio);

    gpio.Pin = RCT6_RS485_DE_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RCT6_RS485_DE_PORT, &gpio);
    HAL_GPIO_WritePin(RCT6_RS485_DE_PORT, RCT6_RS485_DE_PIN, GPIO_PIN_RESET);

    g_rs485_uart.Instance = USART2;
    g_rs485_uart.Init.BaudRate = RS485_BAUDRATE;
    /* F103在启用偶校验时，9位字长包含1位校验位，实际数据仍为8位。 */
    g_rs485_uart.Init.WordLength = UART_WORDLENGTH_9B;
    g_rs485_uart.Init.StopBits = UART_STOPBITS_1;
    g_rs485_uart.Init.Parity = UART_PARITY_EVEN;
    g_rs485_uart.Init.Mode = UART_MODE_TX_RX;
    g_rs485_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_rs485_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_rs485_uart) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_rs485_receiving_length = 0U;
    g_rs485_frame_length = 0U;
    g_rs485_frame_ready = 0U;

    __HAL_UART_CLEAR_IDLEFLAG(&g_rs485_uart);
    __HAL_UART_ENABLE_IT(&g_rs485_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&g_rs485_uart, UART_IT_IDLE);
    __HAL_UART_ENABLE_IT(&g_rs485_uart, UART_IT_ERR);
    /* 优先级6可被FreeRTOS临界区保护，避免任务取帧时USART2抢入修改状态。 */
    HAL_NVIC_SetPriority(USART2_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    return HAL_OK;
}

/**
 * @brief  通过半双工RS485发送一段数据
 * @param  data [输入] 待发送数据缓冲区
 * @param  length [输入] 数据字节数
 * @return HAL_OK表示发送完成; 其他值表示参数错误或USART2发送失败
 * @note   发送前PB8置高; 阻塞发送等待TC置位后再拉低PB8, 防止截断最后一个字节.
 * @example RS485_Send(buffer, sizeof(buffer));
 */
HAL_StatusTypeDef RS485_Send(const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef result;

    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    HAL_GPIO_WritePin(RCT6_RS485_DE_PORT, RCT6_RS485_DE_PIN, GPIO_PIN_SET);
    result = HAL_UART_Transmit(&g_rs485_uart, (uint8_t *)data, length, 1000U);
    while ((__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_TC) == RESET) &&
           (result == HAL_OK))
    {
    }
    HAL_GPIO_WritePin(RCT6_RS485_DE_PORT, RCT6_RS485_DE_PIN, GPIO_PIN_RESET);

    return result;
}

/**
 * @brief  从RS485中断接收区取出一帧数据
 * @param  destination [输出] 用户接收缓冲区
 * @param  capacity [输入] 用户缓冲区容量
 * @return 实际复制的字节数; 0表示当前没有完整帧
 * @note   复制时短暂屏蔽中断, 防止USART2中断与主循环同时访问帧状态.
 * @example length = RS485_ReadFrame(buffer, sizeof(buffer));
 */
uint16_t RS485_ReadFrame(uint8_t *destination, uint16_t capacity)
{
    uint16_t length;
    uint32_t primask;

    if ((destination == NULL) || (capacity == 0U) || (g_rs485_frame_ready == 0U))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    length = g_rs485_frame_length;
    if (length > capacity)
    {
        length = capacity;
    }
    memcpy(destination, g_rs485_frame, length);
    g_rs485_frame_ready = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return length;
}

/**
 * @brief  处理USART2的RS485接收和帧结束中断
 * @param  无
 * @return 无
 * @note   RXNE逐字节收取; IDLE表示线路空闲一个字符时间, 用作可变长度帧边界.
 * @example 在USART2_IRQHandler()中调用.
 */
void RS485_USART_IRQHandler(void)
{
    uint32_t status = g_rs485_uart.Instance->SR;

    if (((status & USART_SR_RXNE) != 0U) &&
        ((g_rs485_uart.Instance->CR1 & USART_CR1_RXNEIE) != 0U))
    {
        uint8_t byte = (uint8_t)g_rs485_uart.Instance->DR;
        if (g_rs485_receiving_length < RS485_RX_BUFFER_SIZE)
        {
            g_rs485_receiving[g_rs485_receiving_length++] = byte;
            modbus_transport_on_byte();
        }
    }

    if (((status & USART_SR_IDLE) != 0U) &&
        ((g_rs485_uart.Instance->CR1 & USART_CR1_IDLEIE) != 0U))
    {
        volatile uint32_t clear_idle;
        clear_idle = g_rs485_uart.Instance->SR;
        clear_idle = g_rs485_uart.Instance->DR;
        (void)clear_idle;
        RS485_PublishFrame();
    }

    if ((status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U)
    {
        volatile uint32_t clear_error;
        clear_error = g_rs485_uart.Instance->SR;
        clear_error = g_rs485_uart.Instance->DR;
        (void)clear_error;
        g_rs485_receiving_length = 0U;
    }
}
