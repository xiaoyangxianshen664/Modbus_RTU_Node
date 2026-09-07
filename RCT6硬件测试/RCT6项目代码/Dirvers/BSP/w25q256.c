#include "w25q256.h"
#include <string.h>

#define W25Q256_CMD_WRITE_ENABLE     0x06U
#define W25Q256_CMD_READ_STATUS1     0x05U
#define W25Q256_CMD_PAGE_PROGRAM     0x02U
#define W25Q256_CMD_READ_DATA        0x03U
#define W25Q256_CMD_SECTOR_ERASE     0x20U
#define W25Q256_CMD_READ_JEDEC_ID    0x9FU
#define W25Q256_STATUS_BUSY          0x01U

#define W25Q256_SPI_TIMEOUT_MS       100U
#define W25Q256_DMA_TIMEOUT_MS       1000U
#define W25Q256_PROGRAM_TIMEOUT_MS   100U
#define W25Q256_ERASE_TIMEOUT_MS     5000U

typedef enum
{
    W25Q256_DMA_IDLE = 0,
    W25Q256_DMA_BUSY,
    W25Q256_DMA_COMPLETE,
    W25Q256_DMA_ERROR
} W25Q256_DMA_StateTypeDef;

SPI_HandleTypeDef g_w25q256_spi;
DMA_HandleTypeDef g_w25q256_tx_dma;
DMA_HandleTypeDef g_w25q256_rx_dma;

static uint8_t g_w25q256_dummy[W25Q256_DMA_MAX_TRANSFER];
static volatile W25Q256_DMA_StateTypeDef g_w25q256_dma_state = W25Q256_DMA_IDLE;

/**
 * @brief  在 SPI 总线上发送 W25Q256 的24位地址
 * @param  command [输入] 本次操作的命令字节
 * @param  address [输入] 0x000000~0xFFFFFF范围内的Flash地址
 * @return HAL_OK表示命令和地址发送成功，其他值表示SPI发送失败
 * @note   本测试只访问16MB以下区域，因此使用W25Q256的三字节地址模式。
 * @example W25Q256_SendCommandAddress(W25Q256_CMD_READ_DATA, 0x000100F0UL);
 */
static HAL_StatusTypeDef W25Q256_SendCommandAddress(uint8_t command, uint32_t address)
{
    uint8_t header[4];

    header[0] = command;
    header[1] = (uint8_t)(address >> 16);
    header[2] = (uint8_t)(address >> 8);
    header[3] = (uint8_t)address;
    return HAL_SPI_Transmit(&g_w25q256_spi, header, sizeof(header), W25Q256_SPI_TIMEOUT_MS);
}

/**
 * @brief  读取 W25Q256 状态寄存器1
 * @param  status [输出] 返回状态寄存器1，其中bit0为BUSY
 * @return HAL_OK表示读取成功，其他值表示参数或SPI通信错误
 * @note   每次读取都形成一次独立的CS低电平事务。
 * @example W25Q256_ReadStatus1(&status);
 */
static HAL_StatusTypeDef W25Q256_ReadStatus1(uint8_t *status)
{
    uint8_t command = W25Q256_CMD_READ_STATUS1;
    uint8_t dummy = 0xFFU;
    HAL_StatusTypeDef result;

    if (status == NULL)
    {
        return HAL_ERROR;
    }

    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&g_w25q256_spi, &command, 1U, W25Q256_SPI_TIMEOUT_MS);
    if (result == HAL_OK)
    {
        result = HAL_SPI_TransmitReceive(&g_w25q256_spi, &dummy, status, 1U,
                                         W25Q256_SPI_TIMEOUT_MS);
    }
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
    return result;
}

/**
 * @brief  等待 W25Q256 内部擦除或编程结束
 * @param  timeout_ms [输入] 最长等待时间，单位毫秒
 * @return HAL_OK表示BUSY已清零，HAL_TIMEOUT表示超时，HAL_ERROR表示通信失败
 * @note   HAL_GetTick()的无符号减法可正确处理毫秒计数器回绕。
 * @example W25Q256_WaitReady(5000U);
 */
static HAL_StatusTypeDef W25Q256_WaitReady(uint32_t timeout_ms)
{
    uint8_t status;
    uint32_t start_tick = HAL_GetTick();
    HAL_StatusTypeDef result;

    do
    {
        result = W25Q256_ReadStatus1(&status);
        if (result != HAL_OK)
        {
            return result;
        }
        if ((status & W25Q256_STATUS_BUSY) == 0U)
        {
            return HAL_OK;
        }
    } while ((HAL_GetTick() - start_tick) < timeout_ms);

    return HAL_TIMEOUT;
}

/**
 * @brief  发送写使能命令
 * @param  无
 * @return HAL_OK表示命令发送成功，其他值表示SPI通信错误
 * @note   W25Q256每次页编程或扇区擦除前都必须重新写使能。
 * @example W25Q256_WriteEnable();
 */
static HAL_StatusTypeDef W25Q256_WriteEnable(void)
{
    uint8_t command = W25Q256_CMD_WRITE_ENABLE;
    HAL_StatusTypeDef result;

    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&g_w25q256_spi, &command, 1U, W25Q256_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
    return result;
}

/**
 * @brief  等待一笔 SPI DMA 传输完成
 * @param  timeout_ms [输入] 最长等待时间，单位毫秒
 * @return HAL_OK表示完成，HAL_ERROR表示DMA出错，HAL_TIMEOUT表示等待超时
 * @note   完成与错误状态由HAL SPI回调函数更新。
 * @example W25Q256_WaitDMA(1000U);
 */
static HAL_StatusTypeDef W25Q256_WaitDMA(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while (g_w25q256_dma_state == W25Q256_DMA_BUSY)
    {
        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            (void)HAL_SPI_Abort(&g_w25q256_spi);
            HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
            g_w25q256_dma_state = W25Q256_DMA_IDLE;
            return HAL_TIMEOUT;
        }
    }

    if (g_w25q256_dma_state == W25Q256_DMA_COMPLETE)
    {
        g_w25q256_dma_state = W25Q256_DMA_IDLE;
        return HAL_OK;
    }

    g_w25q256_dma_state = W25Q256_DMA_IDLE;
    return HAL_ERROR;
}

/**
 * @brief  使用 DMA 编程一页以内的数据
 * @param  data [输入] 待写入数据首地址
 * @param  address [输入] 写入起始地址
 * @param  length [输入] 本次写入字节数，不能跨越256字节页边界
 * @return HAL_OK表示数据已编程完成，其他值表示参数、SPI、DMA或超时错误
 * @note   命令和地址轮询发送，真正的数据段由DMA1_Channel3发送。
 * @example W25Q256_ProgramPageDMA(data, 0x000100F0UL, 16U);
 */
static HAL_StatusTypeDef W25Q256_ProgramPageDMA(const uint8_t *data,
                                                uint32_t address,
                                                uint16_t length)
{
    HAL_StatusTypeDef result;

    if ((data == NULL) || (length == 0U) || (length > W25Q256_PAGE_SIZE) ||
        (((address & (W25Q256_PAGE_SIZE - 1U)) + length) > W25Q256_PAGE_SIZE))
    {
        return HAL_ERROR;
    }

    result = W25Q256_WaitReady(W25Q256_PROGRAM_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }
    result = W25Q256_WriteEnable();
    if (result != HAL_OK)
    {
        return result;
    }

    g_w25q256_dma_state = W25Q256_DMA_BUSY;
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = W25Q256_SendCommandAddress(W25Q256_CMD_PAGE_PROGRAM, address);
    if (result == HAL_OK)
    {
        result = HAL_SPI_Transmit_DMA(&g_w25q256_spi, (uint8_t *)data, length);
    }
    if (result != HAL_OK)
    {
        HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
        g_w25q256_dma_state = W25Q256_DMA_IDLE;
        return result;
    }

    result = W25Q256_WaitDMA(W25Q256_DMA_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }
    return W25Q256_WaitReady(W25Q256_PROGRAM_TIMEOUT_MS);
}

/**
 * @brief  初始化 SPI1、片选GPIO以及收发DMA
 * @param  无
 * @return HAL_OK表示初始化成功，其他值表示SPI或DMA初始化失败
 * @note   RCT6固定映射：SPI1_RX=DMA1_Channel2，SPI1_TX=DMA1_Channel3。
 * @example status = W25Q256_Init();
 */
HAL_StatusTypeDef W25Q256_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio.Pin = RCT6_FLASH_SCK_PIN | RCT6_FLASH_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = RCT6_FLASH_MISO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
    gpio.Pin = RCT6_FLASH_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RCT6_FLASH_CS_PORT, &gpio);

    g_w25q256_spi.Instance = RCT6_FLASH_SPI;
    g_w25q256_spi.Init.Mode = SPI_MODE_MASTER;
    g_w25q256_spi.Init.Direction = SPI_DIRECTION_2LINES;
    g_w25q256_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    g_w25q256_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    g_w25q256_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    g_w25q256_spi.Init.NSS = SPI_NSS_SOFT;
    g_w25q256_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    g_w25q256_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    g_w25q256_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    g_w25q256_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    g_w25q256_spi.Init.CRCPolynomial = 7U;
    if (HAL_SPI_Init(&g_w25q256_spi) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_w25q256_tx_dma.Instance = DMA1_Channel3;
    g_w25q256_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    g_w25q256_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_w25q256_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_w25q256_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_w25q256_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_w25q256_tx_dma.Init.Mode = DMA_NORMAL;
    g_w25q256_tx_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&g_w25q256_tx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_w25q256_rx_dma.Instance = DMA1_Channel2;
    g_w25q256_rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_w25q256_rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_w25q256_rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_w25q256_rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_w25q256_rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_w25q256_rx_dma.Init.Mode = DMA_NORMAL;
    g_w25q256_rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&g_w25q256_rx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __HAL_LINKDMA(&g_w25q256_spi, hdmatx, g_w25q256_tx_dma);
    __HAL_LINKDMA(&g_w25q256_spi, hdmarx, g_w25q256_rx_dma);

    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

    memset(g_w25q256_dummy, 0xFF, sizeof(g_w25q256_dummy));
    g_w25q256_dma_state = W25Q256_DMA_IDLE;
    return HAL_OK;
}

/**
 * @brief  读取 W25Q256 的 JEDEC ID
 * @param  无
 * @return 24位JEDEC ID，正常应为0xEF4019；通信失败返回0
 * @note   依次读取制造商、存储类型和容量三个字节。
 * @example id = W25Q256_ReadJedecId();
 */
uint32_t W25Q256_ReadJedecId(void)
{
    uint8_t command = W25Q256_CMD_READ_JEDEC_ID;
    uint8_t dummy[3] = {0xFFU, 0xFFU, 0xFFU};
    uint8_t response[3] = {0};
    HAL_StatusTypeDef result;

    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&g_w25q256_spi, &command, 1U, W25Q256_SPI_TIMEOUT_MS);
    if (result == HAL_OK)
    {
        result = HAL_SPI_TransmitReceive(&g_w25q256_spi, dummy, response, 3U,
                                         W25Q256_SPI_TIMEOUT_MS);
    }
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
    if (result != HAL_OK)
    {
        return 0U;
    }

    return ((uint32_t)response[0] << 16) |
           ((uint32_t)response[1] << 8) |
           (uint32_t)response[2];
}

/**
 * @brief  擦除指定地址所在的4KB扇区
 * @param  address [输入] 扇区内任意地址，驱动会自动对齐到4KB边界
 * @return HAL_OK表示擦除完成，其他值表示通信或超时错误
 * @note   测试使用0x00010000扇区，不占用项目配置槽0x001000和0x002000。
 * @example W25Q256_EraseSector(0x00010000UL);
 */
HAL_StatusTypeDef W25Q256_EraseSector(uint32_t address)
{
    HAL_StatusTypeDef result;

    address &= ~(W25Q256_SECTOR_SIZE - 1UL);
    result = W25Q256_WaitReady(W25Q256_ERASE_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }
    result = W25Q256_WriteEnable();
    if (result != HAL_OK)
    {
        return result;
    }

    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = W25Q256_SendCommandAddress(W25Q256_CMD_SECTOR_ERASE, address);
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
    if (result != HAL_OK)
    {
        return result;
    }
    return W25Q256_WaitReady(W25Q256_ERASE_TIMEOUT_MS);
}

/**
 * @brief  使用 DMA 写入任意长度数据并自动处理跨页
 * @param  data [输入] 待写入数据首地址
 * @param  address [输入] 写入起始地址
 * @param  length [输入] 写入总字节数
 * @return HAL_OK表示全部写入完成，其他值表示参数、通信或超时错误
 * @note   每次最多编程到当前256字节页末尾，再自动进入下一页。
 * @example W25Q256_WriteDMA(buffer, 0x000100F0UL, 32U);
 */
HAL_StatusTypeDef W25Q256_WriteDMA(const uint8_t *data,
                                   uint32_t address,
                                   uint32_t length)
{
    uint16_t page_length;
    uint32_t page_remaining;
    HAL_StatusTypeDef result;

    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    while (length > 0U)
    {
        page_remaining = W25Q256_PAGE_SIZE - (address & (W25Q256_PAGE_SIZE - 1U));
        page_length = (uint16_t)((length < page_remaining) ? length : page_remaining);
        result = W25Q256_ProgramPageDMA(data, address, page_length);
        if (result != HAL_OK)
        {
            return result;
        }
        data += page_length;
        address += page_length;
        length -= page_length;
    }

    return HAL_OK;
}

/**
 * @brief  使用 SPI1 收发DMA连续读取W25Q256数据
 * @param  data [输出] 保存读回数据的缓冲区
 * @param  address [输入] 读取起始地址
 * @param  length [输入] 读取字节数，范围1~256
 * @return HAL_OK表示读取完成，其他值表示参数、通信或超时错误
 * @note   DMA1_Channel3发送0xFF产生时钟，DMA1_Channel2同步接收Flash数据。
 * @example W25Q256_ReadDMA(buffer, 0x000100F0UL, 32U);
 */
HAL_StatusTypeDef W25Q256_ReadDMA(uint8_t *data, uint32_t address, uint16_t length)
{
    HAL_StatusTypeDef result;

    if ((data == NULL) || (length == 0U) || (length > W25Q256_DMA_MAX_TRANSFER))
    {
        return HAL_ERROR;
    }

    result = W25Q256_WaitReady(W25Q256_PROGRAM_TIMEOUT_MS);
    if (result != HAL_OK)
    {
        return result;
    }

    g_w25q256_dma_state = W25Q256_DMA_BUSY;
    HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_RESET);
    result = W25Q256_SendCommandAddress(W25Q256_CMD_READ_DATA, address);
    if (result == HAL_OK)
    {
        result = HAL_SPI_TransmitReceive_DMA(&g_w25q256_spi,
                                             g_w25q256_dummy,
                                             data,
                                             length);
    }
    if (result != HAL_OK)
    {
        HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
        g_w25q256_dma_state = W25Q256_DMA_IDLE;
        return result;
    }

    return W25Q256_WaitDMA(W25Q256_DMA_TIMEOUT_MS);
}

/**
 * @brief  处理 W25Q256 SPI1 TX DMA 中断
 * @param  无
 * @return 无
 * @note   交给HAL推进页编程发送或全双工读取状态机。
 * @example 由DMA1_Channel3_IRQHandler()调用。
 */
void W25Q256_TX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_w25q256_tx_dma);
}

/**
 * @brief  处理 W25Q256 SPI1 RX DMA 中断
 * @param  无
 * @return 无
 * @note   交给HAL完成全双工读取并触发完成回调。
 * @example 由DMA1_Channel2_IRQHandler()调用。
 */
void W25Q256_RX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_w25q256_rx_dma);
}

/**
 * @brief  HAL SPI DMA 仅发送完成回调
 * @param  hspi [输入] 触发回调的SPI句柄
 * @return 无
 * @note   页编程数据发送结束后释放CS，并通知等待函数。
 * @example 由HAL DMA中断处理流程自动调用。
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == RCT6_FLASH_SPI)
    {
        HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
        g_w25q256_dma_state = W25Q256_DMA_COMPLETE;
    }
}

/**
 * @brief  HAL SPI DMA 全双工收发完成回调
 * @param  hspi [输入] 触发回调的SPI句柄
 * @return 无
 * @note   DMA读取结束后释放CS，并通知等待函数。
 * @example 由HAL DMA中断处理流程自动调用。
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == RCT6_FLASH_SPI)
    {
        HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
        g_w25q256_dma_state = W25Q256_DMA_COMPLETE;
    }
}

/**
 * @brief  HAL SPI 错误回调
 * @param  hspi [输入] 发生错误的SPI句柄
 * @return 无
 * @note   发生DMA或SPI错误时立即释放CS，防止Flash事务一直未结束。
 * @example 由HAL SPI/DMA中断处理流程自动调用。
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == RCT6_FLASH_SPI)
    {
        HAL_GPIO_WritePin(RCT6_FLASH_CS_PORT, RCT6_FLASH_CS_PIN, GPIO_PIN_SET);
        g_w25q256_dma_state = W25Q256_DMA_ERROR;
    }
}
