#include "config_storage.h"
#include "w25q256.h"
#include "modbus_crc16.h"
#include "modbus_registers.h"
#include <stddef.h>
#include <string.h>

#define CONFIG_RECORD_MAGIC        0x43464731UL
#define CONFIG_RECORD_VERSION      1U
#define CONFIG_FLASH_SLOT_A_ADDR   0x001000UL
#define CONFIG_FLASH_SLOT_B_ADDR   0x002000UL

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint16_t holding[MODBUS_HOLDING_REGISTER_COUNT];
    uint16_t crc;
} config_record_t;

static uint8_t g_storage_ready;
static uint8_t g_active_slot;
static uint32_t g_sequence;

/** 初始化已在硬件测试中验证过的W25Q256驱动。 */
void config_storage_init(void)
{
    g_storage_ready = 0U;
    g_active_slot = 0U;
    g_sequence = 0U;
    if ((W25Q256_Init() == HAL_OK) &&
        (W25Q256_ReadJedecId() == W25Q256_JEDEC_ID))
    {
        g_storage_ready = 1U;
    }
}

uint8_t config_storage_is_ready(void)
{
    return g_storage_ready;
}

static uint16_t config_record_crc(const config_record_t *record)
{
    return modbus_crc16((const uint8_t *)record, offsetof(config_record_t, crc));
}

/** 同时检查记录标识、版本、CRC和四个业务参数的范围。 */
static uint8_t config_record_is_valid(const config_record_t *record)
{
    if ((record->magic != CONFIG_RECORD_MAGIC) ||
        (record->version != CONFIG_RECORD_VERSION) ||
        (record->crc != config_record_crc(record)))
    {
        return 0U;
    }
    if ((record->holding[0] == 0U) ||
        (record->holding[1] < 200U) || (record->holding[1] > 800U) ||
        (record->holding[2] < 200U) || (record->holding[2] > 800U) ||
        (record->holding[3] > 1U))
    {
        return 0U;
    }
    return 1U;
}

/** 上电时从A、B双槽中选择sequence较大的最新有效配置。 */
uint8_t config_storage_load_holding(uint16_t *holding)
{
    config_record_t slot_a;
    config_record_t slot_b;
    const config_record_t *selected = NULL;

    if ((holding == NULL) || (g_storage_ready == 0U))
    {
        return 0U;
    }
    if ((W25Q256_ReadDMA((uint8_t *)&slot_a, CONFIG_FLASH_SLOT_A_ADDR,
                         (uint16_t)sizeof(slot_a)) != HAL_OK) ||
        (W25Q256_ReadDMA((uint8_t *)&slot_b, CONFIG_FLASH_SLOT_B_ADDR,
                         (uint16_t)sizeof(slot_b)) != HAL_OK))
    {
        return 0U;
    }

    if (config_record_is_valid(&slot_a) != 0U)
    {
        selected = &slot_a;
        g_active_slot = 0U;
    }
    if ((config_record_is_valid(&slot_b) != 0U) &&
        ((selected == NULL) || (slot_b.sequence > selected->sequence)))
    {
        selected = &slot_b;
        g_active_slot = 1U;
    }
    if (selected == NULL)
    {
        return 0U;
    }

    memcpy(holding, selected->holding, sizeof(selected->holding));
    g_sequence = selected->sequence;
    return 1U;
}

/** 修改配置后写入当前有效槽的另一槽，读回校验成功后才切换活动槽。 */
uint8_t config_storage_save_holding(const uint16_t *holding)
{
    config_record_t record;
    config_record_t verify;
    uint32_t target_addr;

    if ((holding == NULL) || (g_storage_ready == 0U))
    {
        return 0U;
    }
    if ((holding[0] == 0U) ||
        (holding[1] < 200U) || (holding[1] > 800U) ||
        (holding[2] < 200U) || (holding[2] > 800U) ||
        (holding[3] > 1U))
    {
        return 0U;
    }

    target_addr = (g_active_slot == 0U) ? CONFIG_FLASH_SLOT_B_ADDR
                                        : CONFIG_FLASH_SLOT_A_ADDR;
    memset(&record, 0, sizeof(record));
    record.magic = CONFIG_RECORD_MAGIC;
    record.version = CONFIG_RECORD_VERSION;
    record.sequence = g_sequence + 1U;
    memcpy(record.holding, holding, sizeof(record.holding));
    record.crc = config_record_crc(&record);

    if ((W25Q256_EraseSector(target_addr) != HAL_OK) ||
        (W25Q256_WriteDMA((const uint8_t *)&record, target_addr,
                          sizeof(record)) != HAL_OK) ||
        (W25Q256_ReadDMA((uint8_t *)&verify, target_addr,
                         (uint16_t)sizeof(verify)) != HAL_OK))
    {
        return 0U;
    }
    if ((memcmp(&record, &verify, sizeof(record)) != 0) ||
        (config_record_is_valid(&verify) == 0U))
    {
        return 0U;
    }

    g_sequence = record.sequence;
    g_active_slot = (uint8_t)(g_active_slot == 0U);
    return 1U;
}
