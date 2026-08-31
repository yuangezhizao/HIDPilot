#include "hidpilot_flash_store.h"

#include <string.h>

#define HIDPILOT_STORE_MAGIC_0 'H'
#define HIDPILOT_STORE_MAGIC_1 'P'
#define HIDPILOT_STORE_MAGIC_2 'S'
#define HIDPILOT_STORE_MAGIC_3 '1'

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) | ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static void write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

bool hidpilot_store_generation_newer(uint32_t candidate, uint32_t reference) {
    return (int32_t)(candidate - reference) > 0;
}

bool hidpilot_store_build_record(const hidpilot_config_t *config, uint32_t generation, uint8_t *sector, size_t capacity) {
    if (sector == NULL || capacity < HIDPILOT_FLASH_SECTOR_SIZE) {
        return false;
    }
    uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
    const size_t config_length = hidpilot_config_encode(config, encoded, sizeof(encoded));
    if (config_length == 0u) {
        return false;
    }
    memset(sector, 0xff, HIDPILOT_FLASH_SECTOR_SIZE);
    sector[0] = HIDPILOT_STORE_MAGIC_0;
    sector[1] = HIDPILOT_STORE_MAGIC_1;
    sector[2] = HIDPILOT_STORE_MAGIC_2;
    sector[3] = HIDPILOT_STORE_MAGIC_3;
    write_u16_le(&sector[4], HIDPILOT_SCHEMA_VERSION);
    write_u16_le(&sector[6], (uint16_t)config_length);
    write_u32_le(&sector[8], generation);
    write_u32_le(&sector[12], hidpilot_crc32(encoded, config_length));
    memcpy(&sector[HIDPILOT_STORE_HEADER_SIZE], encoded, config_length);
    return true;
}

bool hidpilot_store_parse_record(const uint8_t *sector, size_t length, hidpilot_config_t *config, uint32_t *generation) {
    if (sector == NULL || config == NULL || generation == NULL || length < HIDPILOT_FLASH_SECTOR_SIZE) {
        return false;
    }
    if (sector[0] != HIDPILOT_STORE_MAGIC_0 || sector[1] != HIDPILOT_STORE_MAGIC_1 ||
        sector[2] != HIDPILOT_STORE_MAGIC_2 || sector[3] != HIDPILOT_STORE_MAGIC_3) {
        return false;
    }
    if (read_u16_le(&sector[4]) != HIDPILOT_SCHEMA_VERSION) {
        return false;
    }
    const uint16_t config_length = read_u16_le(&sector[6]);
    if (config_length < HIDPILOT_CONFIG_HEADER_SIZE || config_length > HIDPILOT_CONFIG_MAX_SIZE) {
        return false;
    }
    const uint8_t *encoded = &sector[HIDPILOT_STORE_HEADER_SIZE];
    if (hidpilot_crc32(encoded, config_length) != read_u32_le(&sector[12])) {
        return false;
    }
    if (hidpilot_config_decode(config, encoded, config_length) != HIDPILOT_CONFIG_OK) {
        return false;
    }
    *generation = read_u32_le(&sector[8]);
    return true;
}

int hidpilot_store_select_records(const uint8_t *slot0, const uint8_t *slot1, hidpilot_config_t *config, uint32_t *generation) {
    hidpilot_config_t config0;
    hidpilot_config_t config1;
    uint32_t generation0 = 0u;
    uint32_t generation1 = 0u;
    const bool valid0 = hidpilot_store_parse_record(slot0, HIDPILOT_FLASH_SECTOR_SIZE, &config0, &generation0);
    const bool valid1 = hidpilot_store_parse_record(slot1, HIDPILOT_FLASH_SECTOR_SIZE, &config1, &generation1);
    if (!valid0 && !valid1) {
        return -1;
    }
    if (valid1 && (!valid0 || hidpilot_store_generation_newer(generation1, generation0))) {
        *config = config1;
        *generation = generation1;
        return 1;
    }
    *config = config0;
    *generation = generation0;
    return 0;
}

#ifdef PICO_ON_DEVICE

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"

_Static_assert(PICO_FLASH_SIZE_BYTES == HIDPILOT_FLASH_SIZE_BYTES, "HIDPilot requires the verified 2 MiB XIAO RP2350 flash size");

typedef struct {
    uint32_t offset;
    const uint8_t *data;
} flash_write_context_t;

static uint8_t flash_sector_buffer[HIDPILOT_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static void __not_in_flash_func(write_sector_callback)(void *parameter) {
    flash_write_context_t *context = (flash_write_context_t *)parameter;
    flash_range_erase(context->offset, HIDPILOT_FLASH_SECTOR_SIZE);
    flash_range_program(context->offset, context->data, HIDPILOT_FLASH_SECTOR_SIZE);
}

void hidpilot_flash_store_load(hidpilot_flash_store_t *store, hidpilot_config_t *config) {
    const uint8_t *slot0 = (const uint8_t *)(XIP_BASE + HIDPILOT_FLASH_SLOT0_OFFSET);
    const uint8_t *slot1 = (const uint8_t *)(XIP_BASE + HIDPILOT_FLASH_SLOT1_OFFSET);
    store->active_slot = (int8_t)hidpilot_store_select_records(slot0, slot1, config, &store->generation);
    if (store->active_slot < 0) {
        store->generation = 0u;
        hidpilot_config_default(config);
    }
}

bool hidpilot_flash_store_save(hidpilot_flash_store_t *store, const hidpilot_config_t *config) {
    const uint32_t generation = store->generation + 1u;
    if (!hidpilot_store_build_record(config, generation, flash_sector_buffer, sizeof(flash_sector_buffer))) {
        return false;
    }
    const int8_t target_slot = store->active_slot == 0 ? 1 : 0;
    const uint32_t offset = target_slot == 0 ? HIDPILOT_FLASH_SLOT0_OFFSET : HIDPILOT_FLASH_SLOT1_OFFSET;
    flash_write_context_t context = {.offset = offset, .data = flash_sector_buffer};
    if (flash_safe_execute(write_sector_callback, &context, 1000u) != PICO_OK) {
        return false;
    }
    const uint8_t *written = (const uint8_t *)(XIP_BASE + offset);
    hidpilot_config_t verified;
    uint32_t verified_generation = 0u;
    if (memcmp(written, flash_sector_buffer, HIDPILOT_FLASH_SECTOR_SIZE) != 0 ||
        !hidpilot_store_parse_record(written, HIDPILOT_FLASH_SECTOR_SIZE, &verified, &verified_generation) ||
        verified_generation != generation) {
        return false;
    }
    store->active_slot = target_slot;
    store->generation = generation;
    return true;
}

#else

void hidpilot_flash_store_load(hidpilot_flash_store_t *store, hidpilot_config_t *config) {
    store->active_slot = -1;
    store->generation = 0u;
    hidpilot_config_default(config);
}

bool hidpilot_flash_store_save(hidpilot_flash_store_t *store, const hidpilot_config_t *config) {
    (void)store;
    (void)config;
    return false;
}

#endif
