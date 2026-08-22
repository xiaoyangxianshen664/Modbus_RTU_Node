#ifndef __FF_GEN_DRV_H
#define __FF_GEN_DRV_H

#include "diskio.h"
#include "ff.h"

/* ── 磁盘 I/O 驱动结构体（5 个函数指针 = diskio 那 5 个函数） ── */
typedef struct
{
  DSTATUS (*disk_initialize) (BYTE);
  DSTATUS (*disk_status)     (BYTE);
  DRESULT (*disk_read)       (BYTE, BYTE*, DWORD, UINT);
  DRESULT (*disk_write)      (BYTE, const BYTE*, DWORD, UINT);
  DRESULT (*disk_ioctl)      (BYTE, BYTE, void*);
} Diskio_drvTypeDef;

/* ── 全局驱动注册表 ── */
typedef struct
{
  uint8_t                 is_initialized[FF_VOLUMES];
  const Diskio_drvTypeDef *drv[FF_VOLUMES];       /* const：SD_Driver 是只读驱动表 */
  uint8_t                 lun[FF_VOLUMES];
  volatile uint8_t        nbr;
} Disk_drvTypeDef;

uint8_t FATFS_LinkDriver(const Diskio_drvTypeDef *drv, char *path);
uint8_t FATFS_UnLinkDriver(char *path);

#endif
