#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdint.h>

void config_storage_init(void);
uint8_t config_storage_is_ready(void);
uint8_t config_storage_load_holding(uint16_t *holding);
uint8_t config_storage_save_holding(const uint16_t *holding);

#endif /* CONFIG_STORAGE_H */
