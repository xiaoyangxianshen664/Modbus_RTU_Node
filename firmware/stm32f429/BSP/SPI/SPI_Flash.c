#include "./SPI/SPI_Flash.h"

SPI_HandleTypeDef hspi_flash;       /* SPI5 句柄 */
volatile uint8_t spi_tx_done;         /* IT 发送完成标志 */
volatile uint8_t spi_rx_done;         /* IT 接收完成标志 */
DMA_HandleTypeDef hdma_spi_tx;        /* SPI5 TX DMA: DMA2_Stream4_Ch2 */
DMA_HandleTypeDef hdma_spi_rx;        /* SPI5 RX DMA: DMA2_Stream3_Ch2 */
static uint8_t spi_dummy[256];        /* IT 读时用的哑发送缓冲区 */

/*
 * ────────────────────────────────
 *  SPI5 + W25Q256 初始化
 * ────────────────────────────────
 */
void SPI_Flash_Init(void)
{
    /* ① 开时钟 */
    SPIx_GPIO_CLK_ENABLE();
    SPIx_CLK_ENABLE();

    /* ② GPIO：SCK/MISO/MOSI 复用推挽，CS 推挽输出 */
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;  // 复用推挽输出
    g.Pull = GPIO_PULLUP;      // 上拉
    g.Speed = GPIO_SPEED_FAST; // 高速
    g.Alternate = SPIx_SCK_AF; // 复用功能 AF5，告诉 GPIO 控制器："这个引脚现在归 SPI5 管"

    g.Pin = SPIx_SCK_PIN;
    HAL_GPIO_Init(SPIx_SCK_PORT, &g);

    g.Pin = SPIx_MISO_PIN;
    HAL_GPIO_Init(SPIx_MISO_PORT, &g);

    g.Pin = SPIx_MOSI_PIN;
    HAL_GPIO_Init(SPIx_MOSI_PORT, &g);

    /* CS 引脚：普通输出 */
    g.Pin = FLASH_CS_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(FLASH_CS_PORT, &g);
    FLASH_CS_HIGH(); /* CS 初始高电平：不选中 */

    /* ③ SPI 句柄 */
    hspi_flash.Instance = SPIx;
    hspi_flash.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; /* SPI5 在 APB2（90MHz，90M/2 = 45MHz */
    hspi_flash.Init.Direction = SPI_DIRECTION_2LINES;            /* 全双工 */
    /* ── 模式 0：CPOL=0, CPHA=0 ── */
    hspi_flash.Init.CLKPolarity = SPI_POLARITY_LOW; /* CPOL=0，空闲低 */
    hspi_flash.Init.CLKPhase = SPI_PHASE_1EDGE;     /* CPHA=0，第一边沿采样 */

    /* ── 模式 3：CPOL=1, CPHA=1 ── */
    // hspi_flash.Init.CLKPolarity = SPI_POLARITY_HIGH;          /* CPOL=1，空闲高 */
    // hspi_flash.Init.CLKPhase = SPI_PHASE_2EDGE;               /* CPHA=1，第二边沿采样 */
    hspi_flash.Init.DataSize = SPI_DATASIZE_8BIT;                // W25Q256 指令和数据都是 8 位
    hspi_flash.Init.FirstBit = SPI_FIRSTBIT_MSB;                 // 高位先行
    hspi_flash.Init.NSS = SPI_NSS_SOFT;                          /* CS 手动软件控制 */
    hspi_flash.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE; // CRC效验
    hspi_flash.Init.TIMode = SPI_TIMODE_DISABLE;                 // 关掉，德州仪器的特殊 SPI 变体，用不到
    hspi_flash.Init.Mode = SPI_MODE_MASTER;                      // 主设备还是从设备：主设备

    HAL_SPI_Init(&hspi_flash);
    __HAL_SPI_ENABLE(&hspi_flash); // I2C 的 `HAL_I2C_Init` 内部自动使能，SPI 需要手动补一刀才能跑起来。

    /* ④ SPI 中断 */
    HAL_NVIC_SetPriority(SPI5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI5_IRQn);
}

/*
 * ────────────────────────────────
 *  封装最底层：逐字节收发（全双工）
 * ────────────────────────────────
 */
uint8_t SPI_Flash_SendByte(uint8_t byte)
{
    uint8_t rx;
    HAL_SPI_TransmitReceive(&hspi_flash, &byte, &rx, 1, 100);
    return rx;
}

/*
 * ────────────────────────────────
 *  中层：写使能 + 等忙碌
 * ────────────────────────────────
 */

/* 发 0x06：告诉芯片"我要写/擦了" */
void SPI_Flash_WriteEnable(void)
{
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_WRITE_ENABLE);
    FLASH_CS_HIGH();
}

/* 死等 WIP 忙标志清零 */
void SPI_Flash_WaitBusy(void)
{
    uint8_t status;
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_READ_STATUS);
    do
    {
        status = SPI_Flash_SendByte(DUMMY_BYTE);
    } while (status & WIP_FLAG);
    FLASH_CS_HIGH();
}

/*
 * ────────────────────────────────
 *  高层：读 ID
 * ────────────────────────────────
 */
uint32_t SPI_Flash_ReadID(void)
{
    uint32_t id;
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_JEDEC_ID);
    id = (uint32_t)SPI_Flash_SendByte(DUMMY_BYTE) << 16;
    id |= (uint32_t)SPI_Flash_SendByte(DUMMY_BYTE) << 8;
    id |= (uint32_t)SPI_Flash_SendByte(DUMMY_BYTE);
    FLASH_CS_HIGH();
    return id;
}

/*
 * ────────────────────────────────
 *  高层：擦除扇区（4KB）
 * ────────────────────────────────
 */
void SPI_Flash_EraseSector(uint32_t addr)
{
    SPI_Flash_WriteEnable();
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_SECTOR_ERASE);    /* 命令 0x20 */
    SPI_Flash_SendByte((addr >> 16) & 0xFF); /* 3 字节地址 */
    SPI_Flash_SendByte((addr >> 8) & 0xFF);
    SPI_Flash_SendByte(addr & 0xFF);
    FLASH_CS_HIGH();
    SPI_Flash_WaitBusy(); /* 等擦完 */
}

/*
 * ────────────────────────────────
 *  高层：写一页（≤256 字节）
 * ────────────────────────────────
 * 功能：向 SPI Flash 的指定地址写入一页数据（页编程）
 * 参数：
 *   pData - 指向待写入数据的缓冲区指针
 *   addr  - Flash 内部目标地址（24 位，需页对齐）
 *   len   - 要写入的字节数（必须 ≤ 256，且不超过当前页剩余空间）
 * 说明：
 *   - 使用前需确保目标区域已擦除（全 0xFF）
 *   - 页编程命令为 0x02
 *   - 地址按 3 字节（高→低）发送
 *   - 该函数会阻塞等待 Flash 内部写操作完成
 */
void SPI_Flash_WritePage(uint8_t *pData, uint32_t addr, uint16_t len)
{
    SPI_Flash_WriteEnable(); // ① 发送写使能命令（0x06），允许 Flash 进行编程
    FLASH_CS_LOW();          // ② 拉低 CS 引脚，选中 Flash 芯片，开始 SPI 通信

    SPI_Flash_SendByte(CMD_PAGE_PROGRAM); // ③ 发送页编程指令（0x02），告知 Flash 后续为写数据
    /* 发送 24 位目标地址（大端序：先高字节，后低字节） */
    SPI_Flash_SendByte((addr >> 16) & 0xFF); // ④ 地址高字节（A23~A16）
    SPI_Flash_SendByte((addr >> 8) & 0xFF);  // ⑤ 地址中间字节（A15~A8）
    SPI_Flash_SendByte(addr & 0xFF);         // ⑥ 地址低字节（A7~A0）

    while (len--)
    {                                 // ⑦ 循环发送所有数据字节
        SPI_Flash_SendByte(*pData++); //     每发一个字节，指针下移，长度减一
    }

    FLASH_CS_HIGH();      // ⑧ 拉高 CS 引脚，取消片选，Flash 启动内部写周期
    SPI_Flash_WaitBusy(); // ⑨ 阻塞等待，直到 Flash 内部编程完成（WIP 位清零）
}

/*
 * ────────────────────────────────
 *  高层：跨页写入（自动处理页边界）
 * ────────────────────────────────
 * 功能：向 SPI Flash 写入任意长度数据，自动拆分为多个页编程操作
 * 参数：
 *   pData - 指向待写入数据的缓冲区指针
 *   addr  - Flash 内部目标起始地址
 *   len   - 要写入的总字节数
 * 说明：
 *   - 内部调用 SPI_Flash_WritePage() 完成实际写入
 *   - 自动计算页边界，防止数据回卷覆盖
 *   - 写入前需确保目标区域已擦除（全 0xFF）
 *   - 不支持超过 Flash 容量的地址越界检查（需调用者保证）
 */
void SPI_Flash_BufferWrite(uint8_t *pData, uint32_t addr, uint32_t len)
{
    /* 计算当前地址在当前页中剩余的可用字节数（到页边界为止） */
    uint32_t remain = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);

    /* 情况1：如果第一页就能完全容纳所有数据 */
    if (len <= remain)
    {
        SPI_Flash_WritePage(pData, addr, len); // 单次页编程即可完成
        return;                                // 直接返回，无需后续处理
    }

    /* 情况2：第一页不能完全容纳，先写满第一页的剩余空间 */
    SPI_Flash_WritePage(pData, addr, remain);
    len -= remain;   // 减去已写入的字节数
    addr += remain;  // 地址推进到下一页起始
    pData += remain; // 数据指针同步推进

    /* 情况3：循环写入完整的页（每页 FLASH_PAGE_SIZE 字节） */
    while (len > FLASH_PAGE_SIZE)
    {
        SPI_Flash_WritePage(pData, addr, FLASH_PAGE_SIZE);
        len -= FLASH_PAGE_SIZE;   // 剩余长度减少一页
        addr += FLASH_PAGE_SIZE;  // 地址跳到下一页
        pData += FLASH_PAGE_SIZE; // 数据指针跳一页
    }

    /* 情况4：最后剩余不足一页的数据 */
    if (len > 0)
    {
        SPI_Flash_WritePage(pData, addr, len);
    }
}

/*
地址 0x00F0 = 240，FLASH_PAGE_SIZE = 256

        页0                          页1                       页2
┌──────────────────┐ ┌──────────────────────────────────┐ ┌──────────────┐
│0 ─── 240─── 255│ │0 ────────────── 255│ │0 ─── 228│
│                │ │                                    │ │            │
│    16字节      │ │           256 字节                  │ │  228 字节   │
│   (第1次写)     │ │          (第2次写)                  │ │  (第3次写)  │
│                │ │                                    │ │            │
└──────────────────┘ └──────────────────────────────────┘ └──────────────┘
      remain = 16          整页                              剩余
代码一步步走

输入: addr = 0x00F0 (240), len = 500, pData → [500 字节数据]

①  remain = 256 - (240 % 256)
           = 256 - 240
           = 16                    ← 当前页还能塞 16 字节

②  len (=500) > remain (=16)?  →  第一页塞不下！
    先写 16 字节到 0x00F0 ~ 0x00FF（页 0 写满）
    len   = 500 - 16  = 484
    addr  = 0x00F0 + 16 = 0x0100（下一页起始）
    pData += 16                   ← 指针后移 16 字节

③  while (484 > 256)?  →  484 > 256，YES！
    写 256 字节到 0x0100 ~ 0x01FF（整页 1）
    len   = 484 - 256 = 228
    addr  = 0x0200
    pData += 256

    while (228 > 256)?  →  228 > 256，NO！退出循环

④  if (228 > 0)?  →  228 > 0，YES！
    写最后的 228 字节到 0x0200 ~ 0x02E3（页 2 前 228 字节）

输出: 共 3 次写操作 —— 16 + 256 + 228 = 500 字节


简单的例子：
输入: addr = 0x0400 (页 4 起始，刚好对齐), len = 100

①  remain = 256 - (0x0400 % 256) = 256 - 0 = 256

②  len (=100) <= remain (=256)?  →  100 <= 256，YES！
    第一页就塞下了，直接写 100 字节到 0x0400 ~ 0x0463
    return ← 结束！

输出: 一次写完，不拆分
*/

/*  ────────────────────────────────
 *  高层：读数据（没有页限制）
 * ────────────────────────────────
 * 功能：从 SPI Flash 的指定地址连续读取任意长度的数据
 * 参数：
 *   pData - 指向接收缓冲区的指针（用于存储读取到的数据）
 *   addr  - Flash 内部源起始地址（24 位地址）
 *   len   - 要读取的字节数
 * 说明：
 *   - 使用标准读命令 0x03，支持连续读取，不受页边界限制
 *   - 读取过程中不需要写使能，也不会触发 Flash 内部忙状态
 *   - 每读取一个字节，通过发送 Dummy 字节（如 0xFF）产生 SCK 时钟
 */
void SPI_Flash_BufferRead(uint8_t *pData, uint32_t addr, uint32_t len)
{
    FLASH_CS_LOW(); // ① 拉低 CS 引脚，选中 Flash 芯片，开始 SPI 通信

    SPI_Flash_SendByte(CMD_READ_DATA); // ② 发送读数据命令（0x03），通知 Flash 准备读数据
    /* 发送 24 位起始地址（大端序：先高字节，后低字节） */
    SPI_Flash_SendByte((addr >> 16) & 0xFF); // ③ 地址高字节（A23~A16）
    SPI_Flash_SendByte((addr >> 8) & 0xFF);  // ④ 地址中间字节（A15~A8）
    SPI_Flash_SendByte(addr & 0xFF);         // ⑤ 地址低字节（A7~A0）

    while (len--)
    {                                                // ⑥ 循环读取指定长度的字节
        uint8_t rx = SPI_Flash_SendByte(DUMMY_BYTE); // ① 发 0xFF，同时收到 1 字节
        *pData = rx;                                 // ② 把这个字节存进缓冲区
        pData++;                                     // ③ 指针移到下一个位置        //     发送 Dummy 字节产生时钟，同时将收到的数据存入缓冲区
    }

    FLASH_CS_HIGH(); // ⑦ 拉高 CS 引脚，取消片选，结束通信
}

/*
 * ══════════════════════ 中断版本 ══════════════════════
 * 命令+地址（4 字节）仍用轮询，批量数据走 HAL_SPI_TransmitReceive_IT
 */

/* ── IT 读：命令+地址轮询，数据段走中断 ── */
/*
pData：存放读取数据的缓冲区
addr：Flash 起始地址
len：要读取的字节数
⚠️ 特点：
命令 + 地址：用轮询方式发送（速度快、简单）
数据部分：用 中断方式接收（不阻塞 CPU）
*/

void SPI_Flash_BufferRead_IT(uint8_t *pData, uint32_t addr, uint32_t len)
{
    spi_rx_done = 0;
    FLASH_CS_LOW();													//拉低开始通信，内部使用了 HAL 函数 HAL_GPIO_WritePin()
    SPI_Flash_SendByte(CMD_READ_DATA);                 /* ① 命令（轮询),不是直接 HAL API，但底层常用 HAL_SPI_Transmit()*/
    SPI_Flash_SendByte((addr >> 16) & 0xFF);           /* ② 3 字节地址（轮询） */
    SPI_Flash_SendByte((addr >> 8) & 0xFF);
    SPI_Flash_SendByte(addr & 0xFF);
    
    for (uint32_t i = 0; i < len; i++)				//这个 for 循环的作用是：为 SPI 接收数据提前准备好“哑发送缓冲区”。
		{																					//你想从 Flash 读数据，就必须同时往 Flash 发数据
			spi_dummy[i] = DUMMY_BYTE;							//只有主机发出了时钟（SCK），从机才会把数据移出来。
		}												//spi_dummy内容不重要，唯一作用是：让 SPI 硬件产生时钟，把 Flash 里的数据“挤”出来
    HAL_SPI_TransmitReceive_IT(&hspi_flash, spi_dummy, pData, len);//HAL 库函数使能 SPI 的 TXEIE + RXNEIE 中断 
		
		/*
		HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(
    SPI_HandleTypeDef *hspi,   // 参数1：SPI 外设的“句柄
    uint8_t *pTxData,          // 参数2：发送缓冲区指针
    uint8_t *pRxData,          // 参数3：接收缓冲区指针
    uint16_t Size              // 参数4：要收/发的 字节数
		);
		*/
		
    /* ④ 中断完成后回调里拉高 CS + 置标志 */
}


/* 以中断方式向 SPI Flash 写入一页数据（单页，需调用者保证不跨页）── */
void SPI_Flash_BufferWrite_IT(uint8_t *pData, uint32_t addr, uint32_t len)
{
    spi_tx_done = 0;
    SPI_Flash_WriteEnable();                            /* ① 写使能（轮询） */
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_PAGE_PROGRAM);               /* ② 命令（轮询） */
    SPI_Flash_SendByte((addr >> 16) & 0xFF);            /* ③ 3 字节地址（轮询） */
    SPI_Flash_SendByte((addr >> 8) & 0xFF);
    SPI_Flash_SendByte(addr & 0xFF);
    /* ④ 数据段走中断 */
    HAL_SPI_TransmitReceive_IT(&hspi_flash, pData, spi_dummy, len);
	
		/*
		HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(
    SPI_HandleTypeDef *hspi,   // 参数1 ：SPI 外设句柄
    uint8_t *pTxData,          // 参数2	：发送缓冲区指针
    uint8_t *pRxData,          // 参数3 ：接收缓冲区指针
    uint16_t Size              // 参数4	：要收/发的 字节数
		);
		*/
	
    /* ⑤ 中断完成后回调里拉高 CS + 置标志 */
}

/* ── 收发完成回调（读和写都走这里）── */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI5)
    {
        FLASH_CS_HIGH();
        spi_tx_done = 1;
        spi_rx_done = 1;
    }
}

/*
 * ══════════════════════ DMA 版本 ══════════════════════
 */

/* SPI5 DMA 初始化 */
void SPI_Flash_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* TX DMA：内存→外设 */
    hdma_spi_tx.Instance                 = DMA2_Stream4;
    hdma_spi_tx.Init.Channel             = DMA_CHANNEL_2;
    hdma_spi_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi_tx.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_spi_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_spi_tx);

    /* RX DMA：外设→内存 */
    hdma_spi_rx.Instance                 = DMA2_Stream3;
    hdma_spi_rx.Init.Channel             = DMA_CHANNEL_2;
    hdma_spi_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_spi_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi_rx.Init.Mode                = DMA_NORMAL;
    hdma_spi_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_spi_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_spi_rx);

    /* 挂到 SPI 句柄上 */
    __HAL_LINKDMA(&hspi_flash, hdmatx, hdma_spi_tx);
    __HAL_LINKDMA(&hspi_flash, hdmarx, hdma_spi_rx);

    /* DMA 中断 */
    HAL_NVIC_SetPriority(DMA2_Stream4_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream4_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

/* ── DMA 写（命令+地址轮询，数据走 DMA）── */
void SPI_Flash_BufferWrite_DMA(uint8_t *pData, uint32_t addr, uint32_t len)
{
    spi_tx_done = 0;
    SPI_Flash_WriteEnable();
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_PAGE_PROGRAM);
    SPI_Flash_SendByte((addr >> 16) & 0xFF);
    SPI_Flash_SendByte((addr >> 8) & 0xFF);
    SPI_Flash_SendByte(addr & 0xFF);
    HAL_SPI_TransmitReceive_DMA(&hspi_flash, pData, spi_dummy, len);
}

/* ── DMA 读（命令+地址轮询，数据走 DMA）── */
void SPI_Flash_BufferRead_DMA(uint8_t *pData, uint32_t addr, uint32_t len)
{
    spi_rx_done = 0;
    FLASH_CS_LOW();
    SPI_Flash_SendByte(CMD_READ_DATA);
    SPI_Flash_SendByte((addr >> 16) & 0xFF);
    SPI_Flash_SendByte((addr >> 8) & 0xFF);
    SPI_Flash_SendByte(addr & 0xFF);
    for (uint32_t i = 0; i < len; i++) spi_dummy[i] = DUMMY_BYTE;
    HAL_SPI_TransmitReceive_DMA(&hspi_flash, spi_dummy, pData, len);
}

/*
 * ══════════════════════ IT 跨页写入 ══════════════════════
 * 大数据量跨页才真正体现 IT/DMA 的价值——每页之间等 done + WaitBusy
 */

void SPI_Flash_BufferWrite_IT_Ex(uint8_t *pData, uint32_t addr, uint32_t len)
{
    uint32_t remain = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);

    /* 一页就能塞下 */
    if (len <= remain)
    {
        SPI_Flash_BufferWrite_IT(pData, addr, len);
        return;
    }

    /* 先写满第一页剩余 */
    SPI_Flash_BufferWrite_IT(pData, addr, remain);
    while (!spi_tx_done);
    SPI_Flash_WaitBusy();

    len   -= remain;
    addr  += remain;
    pData += remain;

    /* 写完整页 */
    while (len > FLASH_PAGE_SIZE)
    {
        SPI_Flash_BufferWrite_IT(pData, addr, FLASH_PAGE_SIZE);
        while (!spi_tx_done);
        SPI_Flash_WaitBusy();
        len   -= FLASH_PAGE_SIZE;
        addr  += FLASH_PAGE_SIZE;
        pData += FLASH_PAGE_SIZE;
    }

    /* 最后剩余 */
    if (len > 0)
    {
        SPI_Flash_BufferWrite_IT(pData, addr, len);
        while (!spi_tx_done);
        SPI_Flash_WaitBusy();
    }
}
