#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "stm32f4xx_hal.h"



/* ── SPI5: PF7=SCK, PF8=MISO, PF9=MOSI, PF6=CS ── */
#define SPIx                     SPI5
#define SPIx_CLK_ENABLE()        __HAL_RCC_SPI5_CLK_ENABLE()
#define SPIx_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOF_CLK_ENABLE()

#define SPIx_SCK_PIN             GPIO_PIN_7
#define SPIx_SCK_PORT            GPIOF
#define SPIx_SCK_AF              GPIO_AF5_SPI5

#define SPIx_MISO_PIN            GPIO_PIN_8
#define SPIx_MISO_PORT           GPIOF
#define SPIx_MISO_AF             GPIO_AF5_SPI5

#define SPIx_MOSI_PIN            GPIO_PIN_9
#define SPIx_MOSI_PORT           GPIOF
#define SPIx_MOSI_AF             GPIO_AF5_SPI5

#define FLASH_CS_PIN             GPIO_PIN_6
#define FLASH_CS_PORT            GPIOF

/* CS 宏：手动片选 */
#define FLASH_CS_LOW()   HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET)  /* 拉低 */
#define FLASH_CS_HIGH()  HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET)   /* 拉高 */ 






/* ── W25Q256 命令 ── */
#define CMD_WRITE_ENABLE        0x06  /* 写使能 */
#define CMD_READ_STATUS         0x05  /* 读状态寄存器 */
#define CMD_READ_DATA           0x03  /* 读数据 */
#define CMD_PAGE_PROGRAM        0x02  /* 页编程（写） */
#define CMD_SECTOR_ERASE        0x20  /* 扇区擦除（4KB）命令是0x20 */
#define CMD_CHIP_ERASE          0xC7  /* 全片擦除 */
#define CMD_JEDEC_ID            0x9F  /* 读 JEDEC ID */
#define CMD_POWER_DOWN          0xB9  /* 掉电模式 */

#define WIP_FLAG                0x01  /* 忙标志掩码 */
#define DUMMY_BYTE              0xFF  /* 哑字节，收发时填充用 */

#define FLASH_PAGE_SIZE         256   /* 一页 256 字节 */
#define FLASH_CHIP_ID          0xEF4019  /* W25Q256 的 JEDEC ID */

extern SPI_HandleTypeDef hspi_flash;

/* ── IT/DMA 标志 ── */
extern volatile uint8_t spi_tx_done;   /* 发送完成 */
extern volatile uint8_t spi_rx_done;   /* 接收完成 */

/* ── DMA 句柄 ── */
extern DMA_HandleTypeDef hdma_spi_tx;
extern DMA_HandleTypeDef hdma_spi_rx;

void SPI_Flash_Init(void);

/* 高层接口（轮询版） */
uint32_t SPI_Flash_ReadID(void);
void     SPI_Flash_EraseSector(uint32_t addr);
void     SPI_Flash_WritePage(uint8_t *pData, uint32_t addr, uint16_t len);
void     SPI_Flash_BufferWrite(uint8_t *pData, uint32_t addr, uint32_t len);
void     SPI_Flash_BufferRead(uint8_t *pData, uint32_t addr, uint32_t len);

/* 高层接口（中断版） */
void     SPI_Flash_BufferWrite_IT(uint8_t *pData, uint32_t addr, uint32_t len);
void     SPI_Flash_BufferRead_IT(uint8_t *pData, uint32_t addr, uint32_t len);
void     SPI_Flash_BufferWrite_IT_Ex(uint8_t *pData, uint32_t addr, uint32_t len);

/* 高层接口（DMA 版） */
void     SPI_Flash_DMA_Init(void);
void     SPI_Flash_BufferWrite_DMA(uint8_t *pData, uint32_t addr, uint32_t len);
void     SPI_Flash_BufferRead_DMA(uint8_t *pData, uint32_t addr, uint32_t len);

/* 底层 */
uint8_t  SPI_Flash_SendByte(uint8_t byte);
void     SPI_Flash_WriteEnable(void);
void     SPI_Flash_WaitBusy(void);

#endif
