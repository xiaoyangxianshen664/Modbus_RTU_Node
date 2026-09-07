#include "printf.h"
#include "usart1.h"
#include <stdarg.h>
#include <stdio.h>

/**
 * @brief  初始化 myprintf 使用的 USART1
 * @param  无
 * @return 无
 * @note   USART1 BSP 同时初始化 PA9/PA10、TX/RX DMA 和 IDLE 中断。
 * @example MyPrintfInit();
 */
void MyPrintfInit(void)
{
    USART1_BSP_Init(115200U);
}

/**
 * @brief  将格式化参数转换成字符串并通过 USART1 DMA 发送
 * @param  format [输入] printf 风格格式字符串
 * @param  arguments [输入] 可变参数列表
 * @return HAL_OK 表示已启动发送，其他值表示格式或发送失败
 * @note   最多发送 199 个有效字符，等待上一次 DMA 发送的最长时间为 100ms。
 * @example USART1_PrintV("value=%u", arguments);
 */
static HAL_StatusTypeDef USART1_PrintV(const char *format, va_list arguments)
{
    char buffer[USART1_TX_DMA_BUFFER_SIZE];
    int length;
    uint32_t start_tick;
    HAL_StatusTypeDef status;

    if (format == NULL)
    {
        return HAL_ERROR;
    }

    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (length < 0)
    {
        return HAL_ERROR;
    }
    if (length >= (int)sizeof(buffer))
    {
        length = (int)sizeof(buffer) - 1;
    }

    start_tick = HAL_GetTick();
    do
    {
        status = USART1_SendDMA((const uint8_t *)buffer, (uint16_t)length);
        if (status != HAL_BUSY)
        {
            return status;
        }
    } while ((HAL_GetTick() - start_tick) < 100U);

    return HAL_TIMEOUT;
}

/**
 * @brief  使用 USART1 DMA 输出格式化字符串
 * @param  format [输入] printf 风格格式字符串及其后续可变参数
 * @return 无
 * @note   为兼容原工程保留 myprintf 接口。
 * @example myprintf("count=%u\r\n", count);
 */
void myprintf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)USART1_PrintV(format, arguments);
    va_end(arguments);
}

/**
 * @brief  使用 USART1 DMA 输出格式化字符串并追加回车换行
 * @param  format [输入] printf 风格格式字符串及其后续可变参数
 * @return 无
 * @note   先等待正文 DMA 完成，再发送固定的 CRLF。
 * @example myprintf_n("USART1 ready");
 */
void myprintf_n(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)USART1_PrintV(format, arguments);
    va_end(arguments);

    myprintf("\r\n");
}
