#include "diskio.h"
#include "sd_card.h"

static DSTATUS g_sd_disk_status = STA_NOINIT;

/**
 * @brief  初始化 FatFs 的0号物理磁盘
 * @param  pdrv [输入] 物理磁盘号，本工程只支持0号SD卡
 * @return 0表示成功，STA_NOINIT表示初始化失败
 * @note   f_mount(..., 1)会通过本函数进入SD_Card_Init()。
 * @example disk_initialize(0U);
 */
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }
    if (SD_Card_Init() == HAL_OK)
    {
        g_sd_disk_status = 0U;
    }
    else
    {
        g_sd_disk_status = STA_NOINIT;
    }
    return g_sd_disk_status;
}

/**
 * @brief  返回 FatFs 的0号物理磁盘状态
 * @param  pdrv [输入] 物理磁盘号
 * @return 0表示已初始化，STA_NOINIT表示不可用
 * @note   状态由最近一次SD卡初始化结果维护。
 * @example disk_status(0U);
 */
DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0U) ? g_sd_disk_status : STA_NOINIT;
}

/**
 * @brief  将 FatFs 扇区读取请求转发给SDIO BSP
 * @param  pdrv [输入] 物理磁盘号
 * @param  buff [输出] 数据接收缓冲区
 * @param  sector [输入] 起始扇区号
 * @param  count [输入] 连续读取扇区数
 * @return RES_OK表示成功，其他值表示设备或参数错误
 * @note   FatFs扇区和SD卡逻辑块均为512字节，可直接映射。
 * @example disk_read(0U, buffer, 0U, 1U);
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((g_sd_disk_status & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    return (SD_Card_ReadBlocks(buff, (uint32_t)sector, count) == HAL_OK)
           ? RES_OK : RES_ERROR;
}

/**
 * @brief  将 FatFs 扇区写入请求转发给SDIO BSP
 * @param  pdrv [输入] 物理磁盘号
 * @param  buff [输入] 待写入数据缓冲区
 * @param  sector [输入] 起始扇区号
 * @param  count [输入] 连续写入扇区数
 * @return RES_OK表示成功，其他值表示设备或参数错误
 * @note   驱动写完后会等待SD卡内部编程结束。
 * @example disk_write(0U, buffer, 10U, 1U);
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((g_sd_disk_status & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    return (SD_Card_WriteBlocks(buff, (uint32_t)sector, count) == HAL_OK)
           ? RES_OK : RES_ERROR;
}

/**
 * @brief  处理 FatFs 查询容量和同步状态等控制命令
 * @param  pdrv [输入] 物理磁盘号
 * @param  cmd [输入] FatFs定义的控制命令
 * @param  buff [输出] 保存命令返回值的缓冲区
 * @return RES_OK表示命令成功，其他值表示不支持或设备错误
 * @note   SD卡擦除块大小暂按一个512字节逻辑块报告。
 * @example disk_ioctl(0U, GET_SECTOR_COUNT, &sector_count);
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    HAL_SD_CardInfoTypeDef card_info;

    if (pdrv != 0U)
    {
        return RES_PARERR;
    }
    if ((g_sd_disk_status & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    if (cmd == CTRL_SYNC)
    {
        return (SD_Card_WaitReady(SD_CARD_READY_TIMEOUT_MS) == HAL_OK)
               ? RES_OK : RES_ERROR;
    }
    if (buff == NULL)
    {
        return RES_PARERR;
    }
    if (SD_Card_GetInfo(&card_info) != HAL_OK)
    {
        return RES_ERROR;
    }

    switch (cmd)
    {
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = (LBA_t)card_info.LogBlockNbr;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)card_info.LogBlockSize;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

/**
 * @brief  为 FatFs 新建文件提供固定时间戳
 * @param  无
 * @return 按FatFs位域编码的2026-09-07 09:00:00
 * @note   RTC测试完成后可改为读取板载RTC的实际时间。
 * @example FatFs创建或更新文件时自动调用。
 */
DWORD get_fattime(void)
{
    return ((DWORD)(2026U - 1980U) << 25) |
           ((DWORD)9U << 21) |
           ((DWORD)7U << 16) |
           ((DWORD)9U << 11);
}
