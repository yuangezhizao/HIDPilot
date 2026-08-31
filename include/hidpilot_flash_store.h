#ifndef HIDPILOT_FLASH_STORE_H
#define HIDPILOT_FLASH_STORE_H

#include "hidpilot_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIDPILOT_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#define HIDPILOT_FLASH_SECTOR_SIZE 4096u
#define HIDPILOT_FLASH_SLOT0_OFFSET (HIDPILOT_FLASH_SIZE_BYTES - 3u * HIDPILOT_FLASH_SECTOR_SIZE)
#define HIDPILOT_FLASH_SLOT1_OFFSET (HIDPILOT_FLASH_SIZE_BYTES - 2u * HIDPILOT_FLASH_SECTOR_SIZE)
#define HIDPILOT_FLASH_UNUSED_OFFSET (HIDPILOT_FLASH_SIZE_BYTES - HIDPILOT_FLASH_SECTOR_SIZE)
#define HIDPILOT_STORE_HEADER_SIZE 16u

typedef struct {
    int8_t active_slot;
    uint32_t generation;
} hidpilot_flash_store_t;

bool hidpilot_store_generation_newer(uint32_t candidate, uint32_t reference);
bool hidpilot_store_build_record(const hidpilot_config_t *config, uint32_t generation, uint8_t *sector, size_t capacity);
bool hidpilot_store_parse_record(const uint8_t *sector, size_t length, hidpilot_config_t *config, uint32_t *generation);
int hidpilot_store_select_records(const uint8_t *slot0, const uint8_t *slot1, hidpilot_config_t *config, uint32_t *generation);
void hidpilot_flash_store_load(hidpilot_flash_store_t *store, hidpilot_config_t *config);
bool hidpilot_flash_store_save(hidpilot_flash_store_t *store, const hidpilot_config_t *config);

#endif
