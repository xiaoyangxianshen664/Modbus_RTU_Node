#include "eeprom.h"

typedef enum
{
    EEPROM_TRANSFER_IDLE = 0,
    EEPROM_TRANSFER_BUSY,
    EEPROM_TRANSFER_COMPLETE,
    EEPROM_TRANSFER_ERROR
} EEPROM_TransferState;

I2C_HandleTypeDef g_eeprom_i2c;
DMA_HandleTypeDef g_eeprom_tx_dma;
DMA_HandleTypeDef g_eeprom_rx_dma;

static volatile EEPROM_TransferState g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;

/**
 * @brief  等待一次EEPROM DMA传输结束
 * @param  timeout_ms [输入] 最长等待时间, 单位为毫秒
 * @return HAL_OK表示DMA完成; HAL_TIMEOUT表示超时; HAL_ERROR表示I2C报错
 * @note   DMA启动函数立即返回, 实际完成状态由I2C回调函数修改.
 * @example result = EEPROM_WaitTransfer(100U);
 */
static HAL_StatusTypeDef EEPROM_WaitTransfer(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while (g_eeprom_transfer_state == EEPROM_TRANSFER_BUSY)
    {
        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
            return HAL_TIMEOUT;
        }
    }

    if (g_eeprom_transfer_state == EEPROM_TRANSFER_COMPLETE)
    {
        g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
        return HAL_OK;
    }

    g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
    return HAL_ERROR;
}

/**
 * @brief  初始化AT24C02C使用的I2C1和收发DMA
 * @param  无
 * @return HAL_OK表示初始化成功, 其他值表示I2C或DMA初始化失败
 * @note   PB6/PB7采用复用开漏并依靠板载上拉; F103的I2C1固定使用DMA1通道6/7.
 * @example if (EEPROM_Init() != HAL_OK) { Error_Handler(); }
 */
HAL_StatusTypeDef EEPROM_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio_init.Pin = RCT6_OLED_SCL_PIN | RCT6_OLED_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    g_eeprom_i2c.Instance = RCT6_EEPROM_I2C;
    g_eeprom_i2c.Init.ClockSpeed = 100000U;
    g_eeprom_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    g_eeprom_i2c.Init.OwnAddress1 = 0U;
    g_eeprom_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    g_eeprom_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    g_eeprom_i2c.Init.OwnAddress2 = 0U;
    g_eeprom_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    g_eeprom_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&g_eeprom_i2c) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_eeprom_tx_dma.Instance = DMA1_Channel6;
    g_eeprom_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    g_eeprom_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_eeprom_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_eeprom_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_eeprom_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_eeprom_tx_dma.Init.Mode = DMA_NORMAL;
    g_eeprom_tx_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&g_eeprom_tx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_eeprom_rx_dma.Instance = DMA1_Channel7;
    g_eeprom_rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_eeprom_rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_eeprom_rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_eeprom_rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_eeprom_rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_eeprom_rx_dma.Init.Mode = DMA_NORMAL;
    g_eeprom_rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&g_eeprom_rx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __HAL_LINKDMA(&g_eeprom_i2c, hdmatx, g_eeprom_tx_dma);
    __HAL_LINKDMA(&g_eeprom_i2c, hdmarx, g_eeprom_rx_dma);

    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

    g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
    return HAL_OK;
}

/**
 * @brief  检查AT24C02C是否在I2C1总线上应答
 * @param  无
 * @return HAL_OK表示器件应答, HAL_ERROR或HAL_TIMEOUT表示无应答
 * @note   HAL使用左移一位后的8位地址, 本板AT24C02C的7位地址为0x50.
 * @example if (EEPROM_IsReady() == HAL_OK) { ... }
 */
HAL_StatusTypeDef EEPROM_IsReady(void)
{
    return HAL_I2C_IsDeviceReady(&g_eeprom_i2c,
                                 RCT6_EEPROM_ADDRESS_HAL,
                                 10U,
                                 10U);
}

/**
 * @brief  使用DMA向AT24C02C写入任意长度数据
 * @param  address [输入] EEPROM内部起始地址, 范围0至255
 * @param  data [输入] 待写入数据缓冲区
 * @param  length [输入] 写入字节数, 不得越过256字节容量
 * @return HAL_OK表示全部写入成功; 其他值表示参数、DMA或器件写周期异常
 * @note   AT24C02C每页8字节, 每页写完先等待内部写周期, 再用ACK确认器件就绪.
 * @example EEPROM_WriteDMA(0x43U, buffer, 32U);
 */
HAL_StatusTypeDef EEPROM_WriteDMA(uint8_t address,
                                  const uint8_t *data,
                                  uint16_t length)
{
    HAL_StatusTypeDef result;
    uint16_t page_remaining;
    uint16_t chunk_length;
    uint16_t current_address = address;

    if ((data == NULL) || (length == 0U) ||
        ((current_address + length) > EEPROM_CAPACITY_BYTES))
    {
        return HAL_ERROR;
    }

    while (length > 0U)
    {
        page_remaining = EEPROM_PAGE_SIZE_BYTES -
                         (current_address % EEPROM_PAGE_SIZE_BYTES);
        chunk_length = (length < page_remaining) ? length : page_remaining;

        g_eeprom_transfer_state = EEPROM_TRANSFER_BUSY;
        result = HAL_I2C_Mem_Write_DMA(&g_eeprom_i2c,
                                       RCT6_EEPROM_ADDRESS_HAL,
                                       current_address,
                                       I2C_MEMADD_SIZE_8BIT,
                                       (uint8_t *)data,
                                       chunk_length);
        if (result != HAL_OK)
        {
            g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
            return result;
        }

        result = EEPROM_WaitTransfer(EEPROM_DMA_TIMEOUT_MS);
        if (result != HAL_OK)
        {
            return result;
        }

        /* AT24C02C最长写周期为5ms。F1 HAL不适合在STOP后立即连续发送NACK探测，
           因此先等待6ms，再用ACK轮询确认器件确实已经可以接收下一页。 */
        HAL_Delay(6U);
        result = EEPROM_IsReady();
        if (result != HAL_OK)
        {
            return result;
        }

        current_address += chunk_length;
        data += chunk_length;
        length -= chunk_length;
    }

    return HAL_OK;
}

/**
 * @brief  使用DMA从AT24C02C连续读取数据
 * @param  address [输入] EEPROM内部起始地址, 范围0至255
 * @param  data [输出] 接收数据缓冲区
 * @param  length [输入] 读取字节数, 不得越过256字节容量
 * @return HAL_OK表示读取成功; 其他值表示参数、DMA或I2C错误
 * @note   连续读取不受页边界限制, 因此可以由一次DMA事务完成.
 * @example EEPROM_ReadDMA(0x43U, buffer, 32U);
 */
HAL_StatusTypeDef EEPROM_ReadDMA(uint8_t address,
                                 uint8_t *data,
                                 uint16_t length)
{
    HAL_StatusTypeDef result;
    uint16_t start_address = address;

    if ((data == NULL) || (length == 0U) ||
        ((start_address + length) > EEPROM_CAPACITY_BYTES))
    {
        return HAL_ERROR;
    }

    g_eeprom_transfer_state = EEPROM_TRANSFER_BUSY;
    result = HAL_I2C_Mem_Read_DMA(&g_eeprom_i2c,
                                  RCT6_EEPROM_ADDRESS_HAL,
                                  start_address,
                                  I2C_MEMADD_SIZE_8BIT,
                                  data,
                                  length);
    if (result != HAL_OK)
    {
        g_eeprom_transfer_state = EEPROM_TRANSFER_IDLE;
        return result;
    }

    return EEPROM_WaitTransfer(EEPROM_DMA_TIMEOUT_MS);
}

/** @brief 转交I2C1事件中断给HAL状态机; @param 无; @return 无 */
void EEPROM_I2C_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&g_eeprom_i2c);
}

/** @brief 转交I2C1错误中断给HAL状态机; @param 无; @return 无 */
void EEPROM_I2C_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&g_eeprom_i2c);
}

/** @brief 转交I2C1发送DMA中断; @param 无; @return 无 */
void EEPROM_TX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_eeprom_tx_dma);
}

/** @brief 转交I2C1接收DMA中断; @param 无; @return 无 */
void EEPROM_RX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_eeprom_rx_dma);
}

/** @brief HAL的I2C存储器发送完成回调; @param hi2c [输入] I2C句柄; @return 无 */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &g_eeprom_i2c)
    {
        g_eeprom_transfer_state = EEPROM_TRANSFER_COMPLETE;
    }
}

/** @brief HAL的I2C存储器接收完成回调; @param hi2c [输入] I2C句柄; @return 无 */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &g_eeprom_i2c)
    {
        g_eeprom_transfer_state = EEPROM_TRANSFER_COMPLETE;
    }
}

/** @brief HAL的I2C错误回调; @param hi2c [输入] I2C句柄; @return 无 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &g_eeprom_i2c)
    {
        g_eeprom_transfer_state = EEPROM_TRANSFER_ERROR;
    }
}
