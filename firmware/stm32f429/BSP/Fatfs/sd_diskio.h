#ifndef __SD_DISKIO_H
#define __SD_DISKIO_H

#include "ff_gen_drv.h"

extern const Diskio_drvTypeDef SD_Driver;    /* SD ¿¨Çý¶¯ÊµÀý */

DSTATUS SD_initialize(BYTE lun);
DSTATUS SD_status(BYTE lun);
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count);
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count);
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff);

#endif
