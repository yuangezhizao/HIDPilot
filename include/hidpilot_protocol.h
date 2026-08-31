#ifndef HIDPILOT_PROTOCOL_H
#define HIDPILOT_PROTOCOL_H

#include "hidpilot_config.h"
#include "hidpilot_executor.h"

#include <stdbool.h>
#include <stdint.h>

#define HIDPILOT_FRAME_SIZE 64u
#define HIDPILOT_FRAME_PAYLOAD_MAX 56u
#define HIDPILOT_PROTOCOL_VERSION 1u

typedef enum {
    HIDPILOT_CMD_GET_STATUS = 0x01,
    HIDPILOT_CMD_GET_CONFIG_INFO = 0x02,
    HIDPILOT_CMD_GET_CONFIG_CHUNK = 0x03,
    HIDPILOT_CMD_STAGE_BEGIN = 0x10,
    HIDPILOT_CMD_STAGE_CHUNK = 0x11,
    HIDPILOT_CMD_STAGE_VALIDATE = 0x12,
    HIDPILOT_CMD_RUN_ONCE = 0x13,
    HIDPILOT_CMD_APPLY_TEMP = 0x14,
    HIDPILOT_CMD_APPLY_SAVE = 0x15,
    HIDPILOT_CMD_RESTORE_DEFAULT = 0x16,
    HIDPILOT_CMD_REBOOT_APPLICATION = 0x17,
    HIDPILOT_CMD_REBOOT_BOOTSEL = 0x18,
} hidpilot_command_t;

typedef enum {
    HIDPILOT_STATUS_OK = 0,
    HIDPILOT_STATUS_MAGIC = 1,
    HIDPILOT_STATUS_VERSION = 2,
    HIDPILOT_STATUS_LENGTH = 3,
    HIDPILOT_STATUS_COMMAND = 4,
    HIDPILOT_STATUS_TRANSACTION = 5,
    HIDPILOT_STATUS_OFFSET = 6,
    HIDPILOT_STATUS_CRC = 7,
    HIDPILOT_STATUS_CONFIG = 8,
    HIDPILOT_STATUS_BUSY = 9,
    HIDPILOT_STATUS_FLASH = 10,
    HIDPILOT_STATUS_INCOMPLETE = 11,
} hidpilot_status_t;

typedef struct {
    void *context;
    bool (*save_config)(void *context, const hidpilot_config_t *config);
    void (*schedule_reboot)(void *context, bool bootsel);
} hidpilot_protocol_io_t;

typedef struct {
    hidpilot_config_t *active_config;
    hidpilot_executor_t *executor;
    hidpilot_protocol_io_t io;
    uint8_t staged_data[HIDPILOT_CONFIG_MAX_SIZE];
    uint8_t staged_received[(HIDPILOT_CONFIG_MAX_SIZE + 7u) / 8u];
    hidpilot_config_t staged_config;
    uint32_t staged_crc;
    uint16_t staged_length;
    uint16_t staged_transaction;
    bool staged_active;
    bool staged_valid;
    uint32_t generation;
    int8_t active_slot;
} hidpilot_protocol_t;

void hidpilot_protocol_init(hidpilot_protocol_t *protocol, hidpilot_config_t *active_config,
                            hidpilot_executor_t *executor, hidpilot_protocol_io_t io);
void hidpilot_protocol_set_storage_status(hidpilot_protocol_t *protocol, int8_t active_slot, uint32_t generation);
void hidpilot_protocol_handle(hidpilot_protocol_t *protocol, const uint8_t request[HIDPILOT_FRAME_SIZE],
                              uint8_t response[HIDPILOT_FRAME_SIZE], uint32_t now_ms);

#endif
