/*-----------------------------------------------------------------------*/
/* FatFs 磁盘 I/O 调度层 — 根据 pdrv 路由到已注册的驱动                  */
/*                                                                       */
/* 不再硬编码特定存储的读写函数，而是通过 disk.drv[pdrv] 函数指针转发。  */
/* main() 里 FATFS_LinkDriver(&SD_Driver, "0:") 注册 SD 卡驱动后，      */
/* 所有 "0:" 的 I/O 操作自动路由到 SD_read/SD_write/SD_ioctl。          */
/*-----------------------------------------------------------------------*/

#include "diskio.h"
#include "ff_gen_drv.h"

extern Disk_drvTypeDef disk;

DSTATUS disk_status(BYTE pdrv)
{
    return disk.drv[pdrv]->disk_status(disk.lun[pdrv]);
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (disk.is_initialized[pdrv] == 0)
    {
        disk.is_initialized[pdrv] = 1;
        return disk.drv[pdrv]->disk_initialize(disk.lun[pdrv]);
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    return disk.drv[pdrv]->disk_read(disk.lun[pdrv], buff, sector, count);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    return disk.drv[pdrv]->disk_write(disk.lun[pdrv], buff, sector, count);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return disk.drv[pdrv]->disk_ioctl(disk.lun[pdrv], cmd, buff);
}

DWORD get_fattime(void)
{
    return  ((DWORD)(2024 - 1980) << 25)
          | ((DWORD)1 << 21)
          | ((DWORD)1 << 16)
          | ((DWORD)0 << 11)
          | ((DWORD)0 << 5)
          | ((DWORD)0 >> 1);
}
