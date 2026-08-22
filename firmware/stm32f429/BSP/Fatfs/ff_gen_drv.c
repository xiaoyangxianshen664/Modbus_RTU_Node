/**
  * @file    ff_gen_drv.c
  * @brief   FatFs 通用驱动管理器——将多个存储驱动注册到 FatFs
  *
  * 调用链：main → FATFS_LinkDriver(&SD_Driver, "0:") → disk.drv[0] = &SD_Driver
  *         FatFs → disk_read(0) → disk.drv[0]->disk_read = SD_read
  */

#include "ff_gen_drv.h"

Disk_drvTypeDef disk = {{0}, {0}, {0}, 0};         /* 全局驱动注册表 */

/* ── FATFS_LinkDriver：注册一个磁盘驱动并自动分配盘符 ── */
uint8_t FATFS_LinkDriverEx(const Diskio_drvTypeDef *drv, char *path, uint8_t lun)
{
    if (disk.nbr >= FF_VOLUMES) return 1;

    disk.is_initialized[disk.nbr] = 0;
    disk.drv[disk.nbr] = drv;
    disk.lun[disk.nbr] = lun;

    uint8_t disk_num = disk.nbr++;
    path[0] = disk_num + '0';       /* "0:", "1:", "2:" ... */
    path[1] = ':';
    path[2] = '/';
    path[3] = 0;
    return 0;
}

uint8_t FATFS_LinkDriver(const Diskio_drvTypeDef *drv, char *path)
{
    return FATFS_LinkDriverEx(drv, path, 0);
}

uint8_t FATFS_UnLinkDriver(char *path)
{
    if (disk.nbr == 0) return 1;

    uint8_t num = path[0] - '0';
    disk.drv[num] = 0;
    disk.lun[num] = 0;
    disk.nbr--;
    return 0;
}
