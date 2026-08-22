/*-----------------------------------------------------------------------*/
/* FatFs 底层磁盘接口头文件  (C)ChaN, 2025                                */
/*                                                                       */
/* 本头文件定义了 FatFs 要求的 5 个桥接函数原型 + 状态/命令宏。          */
/* 所有函数在 diskio.c 中实现，换存储介质时只改 diskio.c 即可。          */
/*-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#include "ff.h"          /* BYTE, UINT, LBA_t 等类型定义来自 ff.h */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 设备状态（DSTATUS 位域） ── */
typedef BYTE	DSTATUS;

/* ── 操作结果（DRESULT 枚举） ── */
typedef enum {
    RES_OK = 0,         /* 0: 成功 */
    RES_ERROR,          /* 1: 读写错误 */
    RES_WRPRT,          /* 2: 写保护 */
    RES_NOTRDY,         /* 3: 设备未就绪 */
    RES_PARERR          /* 4: 参数无效 */
} DRESULT;

/* ── 5 个桥接函数原型（↓ 全部在 diskio.c 中实现 ↓） ── */

DSTATUS disk_initialize (BYTE pdrv);                                    /* 初始化存储设备 */
DSTATUS disk_status (BYTE pdrv);                                        /* 获取设备状态 */
DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);    /* 读扇区 */
DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count); /* 写扇区 */
DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff);                   /* 设备控制 */

/* ── DSTATUS 状态位 ── */
#define STA_NOINIT		0x01	/* 设备未初始化 */
#define STA_NODISK		0x02	/* 无存储介质 */
#define STA_PROTECT		0x04	/* 写保护 */

/* ── disk_ioctl 命令码 ── */
#define CTRL_SYNC			0	/* 完成挂起写操作（Flash 无需实现） */
#define GET_SECTOR_COUNT	1	/* 获取总扇区数（f_mkfs 需要） */
#define GET_SECTOR_SIZE		2	/* 获取扇区大小（变长扇区模式需要） */
#define GET_BLOCK_SIZE		3	/* 获取擦除块大小（f_mkfs 需要） */
#define CTRL_TRIM			4	/* 通知设备该区域不再使用（SSD 用，Flash 不管） */

#ifdef __cplusplus
}
#endif

#endif
