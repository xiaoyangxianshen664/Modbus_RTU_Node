/**
  * @file    sd_diskio.c
  * @brief   SD 卡磁盘 I/O 驱动（FatFs ↔ BSP_SD 桥接，轮询模式）
  */

#include "sd_diskio.h"
#include "../BSP/SDIO/bsp_sdio_sd.h"
#include <stdio.h>

/* ════════════════════════════════════════════════════════════════
 * SD_Driver — 将 5 个 I/O 函数打包成 FatFs 认识的驱动对象
 * ════════════════════════════════════════════════════════════════
 */
const Diskio_drvTypeDef SD_Driver =
{
    SD_initialize,
    SD_status,
    SD_read,
    SD_write,
    SD_ioctl,
};

/* ════════════════════════════════════════════════════════════════
 * SD_initialize — 初始化 SD 卡
 * ════════════════════════════════════════════════════════════════
 */
DSTATUS SD_initialize(BYTE lun)
{
    if (BSP_SD_Init() == MSD_OK)
    {
        HAL_SD_CardInfoTypeDef info;
        BSP_SD_GetCardInfo(&info);
        printf("[卡信息] Type=%lu 块数=%lu 块大小=%lu\r\n",
               (unsigned long)info.CardType,
               (unsigned long)info.LogBlockNbr,
               (unsigned long)info.LogBlockSize);
        return 0;
    }
    return STA_NOINIT;
}

/* ════════════════════════════════════════════════════════════════
 * SD_status — 查询 SD 卡状态
 * ════════════════════════════════════════════════════════════════
 */
DSTATUS SD_status(BYTE lun)
{
    if (BSP_SD_GetCardState() == SD_TRANSFER_OK) return 0;
    return STA_NOINIT;
}

/* ════════════════════════════════════════════════════════════════
 * SD_read — 从 SD 卡读取 N 个扇区（DMA）
 *
 * 原型：DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
 *       buff   — [输出] 读取数据存放地址
 *       sector — [输入] 起始扇区号（一个扇区 512 字节）
 *       count  — [输入] 连续读取扇区数
 * 返值：RES_OK=成功，RES_ERROR=失败
 *
 * 调用示例：
 *   SD_read(0, buf, 0, 1);  // 读第 0 扇区 512 字节到 buf
 * ════════════════════════════════════════════════════════════════
 */
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
    uint32_t wait = 1000000;                                        /* 超时保护计数 */

    if (BSP_SD_ReadBlocks_DMA((uint32_t *)buff, sector, count) != MSD_OK)
        return RES_ERROR;                                           /* DMA 启动失败 */

    while (BSP_SD_GetCardState() != SD_TRANSFER_OK)                 /* 等卡空闲：DMA 搬完 + 多块读 STOP */
    {
        if (--wait == 0) return RES_ERROR;                          /* 超时保护 */
    }
    return RES_OK;
}

/* ════════════════════════════════════════════════════════════════
 * SD_write — 向 SD 卡写入 N 个扇区（DMA）
 *
 * 原型：DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
 *       buff   — [输入] 待写入数据的地址
 *       sector — [输入] 起始扇区号
 *       count  — [输入] 连续写入扇区数
 * 返值：RES_OK=成功，RES_ERROR=失败
 *
 * 调用示例：
 *   SD_write(0, data, 0, 1);  // 写 512 字节到第 0 扇区
 * ════════════════════════════════════════════════════════════════
 */
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
    uint32_t wait = 1000000;                                        /* 超时保护计数 */

    if (BSP_SD_WriteBlocks_DMA((uint32_t *)buff, sector, count) != MSD_OK)
        return RES_ERROR;                                           /* DMA 启动失败 */

    while (BSP_SD_GetCardState() != SD_TRANSFER_OK)                 /* 等卡空闲：DMA 搬完 + 卡编程完成 */
    {
        if (--wait == 0) return RES_ERROR;                          /* 超时保护 */
    }
    return RES_OK;
}

/* ════════════════════════════════════════════════════════════════
 * SD_ioctl — SD 卡控制（返回总扇区数/扇区大小/擦除块大小）
 *
 * 原型：DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
 *       cmd  — [输入] 控制命令
 *       buff — [输出] 返回数据存放地址
 * 返值：RES_OK=成功
 *
 * 调用示例：
 *   DWORD count; SD_ioctl(0, GET_SECTOR_COUNT, &count);
 * ════════════════════════════════════════════════════════════════
 */
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
    HAL_SD_CardInfoTypeDef info;

    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            BSP_SD_GetCardInfo(&info);
            *(DWORD *)buff = info.LogBlockNbr;
            return RES_OK;

        case GET_SECTOR_SIZE:
            BSP_SD_GetCardInfo(&info);
            *(WORD *)buff = info.LogBlockSize;
            return RES_OK;

        case GET_BLOCK_SIZE:
            BSP_SD_GetCardInfo(&info);
            *(DWORD *)buff = info.LogBlockSize / 512;
            return RES_OK;
    }
    return RES_PARERR;
}
