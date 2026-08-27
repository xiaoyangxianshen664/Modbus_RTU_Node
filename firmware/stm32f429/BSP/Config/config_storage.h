#ifndef __CONFIG_STORAGE_H
#define __CONFIG_STORAGE_H

#include <stdint.h>

/*
 * W25Q256 配置持久化模块
 *
 * 功能：
 *   1. 保存 4 个 Modbus 保持寄存器；
 *   2. 启动时从 W25Q256 恢复上次有效配置；
 *   3. 使用槽 A、槽 B 两个 4KB 扇区交替保存；
 *   4. 通过 magic、版本号、序号和 CRC 判断记录是否有效。
 *
 * 当前只使用 W25Q256 的低地址区域，因此使用 24 位地址即可。
 */
 
 /*
 * 配置槽 A：
 * 地址范围：0x001000 ~ 0x001FFF
 * 大小：4KB
 */
#define CONFIG_FLASH_SLOT_A_ADDR 0x001000U

/*
 * 配置槽 B：
 * 地址范围：0x002000 ~ 0x002FFF
 * 大小：4KB
 */
#define CONFIG_FLASH_SLOT_B_ADDR 0x002000U



  

void    config_storage_init(void);													/* 初始化 SPI5 和 W25Q256，并读取 JEDEC ID 判断外部 Flash 是否可用。*/
uint8_t config_storage_is_ready(void);											/*查询配置存储是否可用，返回值1：W25Q256 已正确识别，0：Flash 不可用或型号不匹配。*/
uint8_t config_storage_load_holding(uint16_t *holding);			/*从槽 A、槽 B 中读取最新的有效配置*/
uint8_t config_storage_save_holding(const uint16_t *holding);/*将新的保持寄存器配置保存到备用槽*/

#endif /* __CONFIG_STORAGE_H */
