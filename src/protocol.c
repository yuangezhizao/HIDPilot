#include "hidpilot_protocol.h"

#include <string.h>

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

static void make_response(uint8_t response[HIDPILOT_FRAME_SIZE], uint8_t command, uint16_t transaction,
                          hidpilot_status_t status, uint8_t payload_length) {
    memset(response, 0, HIDPILOT_FRAME_SIZE);
    response[0] = 'H';
    response[1] = 'P';
    response[2] = HIDPILOT_PROTOCOL_VERSION;
    response[3] = (uint8_t)(command | 0x80u);
    write_u16_le(&response[4], transaction);
    response[6] = payload_length;
    response[7] = (uint8_t)status;
}

static bool same_stage_transaction(const hidpilot_protocol_t *protocol, uint16_t transaction) {
    return protocol->staged_active && protocol->staged_transaction == transaction;
}

static void mark_received(hidpilot_protocol_t *protocol, uint16_t offset, uint8_t length) {
    for (uint16_t index = offset; index < (uint16_t)(offset + length); ++index) {
        protocol->staged_received[index / 8u] |= (uint8_t)(1u << (index % 8u));
    }
}

static bool all_received(const hidpilot_protocol_t *protocol) {
    for (uint16_t index = 0; index < protocol->staged_length; ++index) {
        if ((protocol->staged_received[index / 8u] & (uint8_t)(1u << (index % 8u))) == 0u) {
            return false;
        }
    }
    return true;
}

static bool encode_active(const hidpilot_protocol_t *protocol, uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE],
                          uint16_t *length, uint32_t *crc) {
    const size_t encoded_length = hidpilot_config_encode(protocol->active_config, encoded, HIDPILOT_CONFIG_MAX_SIZE);
    if (encoded_length == 0u) {
        return false;
    }
    *length = (uint16_t)encoded_length;
    *crc = hidpilot_crc32(encoded, encoded_length);
    return true;
}

void hidpilot_protocol_init(hidpilot_protocol_t *protocol, hidpilot_config_t *active_config,
                            hidpilot_executor_t *executor, hidpilot_protocol_io_t io) {
    memset(protocol, 0, sizeof(*protocol));
    protocol->active_config = active_config;
    protocol->executor = executor;
    protocol->io = io;
    protocol->active_slot = -1;
}

void hidpilot_protocol_set_storage_status(hidpilot_protocol_t *protocol, int8_t active_slot, uint32_t generation) {
    protocol->active_slot = active_slot;
    protocol->generation = generation;
}

static hidpilot_status_t validate_stage(hidpilot_protocol_t *protocol) {
    if (!all_received(protocol)) {
        return HIDPILOT_STATUS_INCOMPLETE;
    }
    if (hidpilot_crc32(protocol->staged_data, protocol->staged_length) != protocol->staged_crc) {
        return HIDPILOT_STATUS_CRC;
    }
    if (hidpilot_config_decode(&protocol->staged_config, protocol->staged_data, protocol->staged_length) != HIDPILOT_CONFIG_OK) {
        return HIDPILOT_STATUS_CONFIG;
    }
    protocol->staged_valid = true;
    return HIDPILOT_STATUS_OK;
}

static void handle_status(hidpilot_protocol_t *protocol, uint8_t response[HIDPILOT_FRAME_SIZE], uint8_t command,
                          uint16_t transaction) {
    uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
    uint16_t length = 0u;
    uint32_t crc = 0u;
    if (!encode_active(protocol, encoded, &length, &crc)) {
        make_response(response, command, transaction, HIDPILOT_STATUS_CONFIG, 0u);
        return;
    }
    make_response(response, command, transaction, HIDPILOT_STATUS_OK, 30u);
    response[8] = HIDPILOT_FIRMWARE_VERSION_MAJOR;
    response[9] = HIDPILOT_FIRMWARE_VERSION_MINOR;
    response[10] = HIDPILOT_FIRMWARE_VERSION_PATCH;
    response[11] = HIDPILOT_SCHEMA_VERSION;
    response[12] = (uint8_t)((protocol->active_config->enabled ? 1u : 0u) |
                             (protocol->executor->mounted ? 2u : 0u) |
                             (protocol->executor->suspended ? 4u : 0u) |
                             (hidpilot_executor_busy(protocol->executor) ? 8u : 0u));
    response[13] = protocol->executor->action_index;
    response[14] = protocol->active_config->action_count;
    response[15] = protocol->active_slot < 0 ? 0u : (uint8_t)(protocol->active_slot + 1);
    write_u32_le(&response[16], protocol->active_config->repeat_interval_ms);
    write_u32_le(&response[20], protocol->executor->completed_runs);
    write_u32_le(&response[24], protocol->executor->error_count);
    write_u32_le(&response[28], protocol->generation);
    write_u32_le(&response[32], crc);
    write_u16_le(&response[36], length);
}

void hidpilot_protocol_handle(hidpilot_protocol_t *protocol, const uint8_t request[HIDPILOT_FRAME_SIZE],
                              uint8_t response[HIDPILOT_FRAME_SIZE], uint32_t now_ms) {
    const uint8_t command = request[3];
    const uint16_t transaction = read_u16_le(&request[4]);
    const uint8_t payload_length = request[6];
    if (request[0] != 'H' || request[1] != 'P') {
        make_response(response, command, transaction, HIDPILOT_STATUS_MAGIC, 0u);
        return;
    }
    if (request[2] != HIDPILOT_PROTOCOL_VERSION) {
        make_response(response, command, transaction, HIDPILOT_STATUS_VERSION, 0u);
        return;
    }
    if (payload_length > HIDPILOT_FRAME_PAYLOAD_MAX || request[7] != 0u) {
        make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
        return;
    }

    switch (command) {
        case HIDPILOT_CMD_GET_STATUS:
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else {
                handle_status(protocol, response, command, transaction);
            }
            break;
        case HIDPILOT_CMD_GET_CONFIG_INFO: {
            uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
            uint16_t length = 0u;
            uint32_t crc = 0u;
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!encode_active(protocol, encoded, &length, &crc)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_CONFIG, 0u);
            } else {
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 10u);
                write_u16_le(&response[8], length);
                write_u32_le(&response[10], crc);
                write_u32_le(&response[14], protocol->generation);
            }
            break;
        }
        case HIDPILOT_CMD_GET_CONFIG_CHUNK: {
            uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
            uint16_t length = 0u;
            uint32_t crc = 0u;
            if (payload_length != 3u || !encode_active(protocol, encoded, &length, &crc)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
                break;
            }
            const uint16_t offset = read_u16_le(&request[8]);
            const uint8_t wanted = request[10];
            if (wanted == 0u || wanted > 54u || offset >= length || (uint32_t)offset + wanted > length) {
                make_response(response, command, transaction, HIDPILOT_STATUS_OFFSET, 0u);
                break;
            }
            make_response(response, command, transaction, HIDPILOT_STATUS_OK, (uint8_t)(wanted + 2u));
            write_u16_le(&response[8], offset);
            memcpy(&response[10], &encoded[offset], wanted);
            break;
        }
        case HIDPILOT_CMD_STAGE_BEGIN:
            if (payload_length != 6u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
                break;
            }
            protocol->staged_length = read_u16_le(&request[8]);
            protocol->staged_crc = read_u32_le(&request[10]);
            if (protocol->staged_length < HIDPILOT_CONFIG_HEADER_SIZE || protocol->staged_length > HIDPILOT_CONFIG_MAX_SIZE) {
                protocol->staged_active = false;
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
                break;
            }
            protocol->staged_transaction = transaction;
            protocol->staged_active = true;
            protocol->staged_valid = false;
            memset(protocol->staged_data, 0, sizeof(protocol->staged_data));
            memset(protocol->staged_received, 0, sizeof(protocol->staged_received));
            make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            break;
        case HIDPILOT_CMD_STAGE_CHUNK: {
            if (!same_stage_transaction(protocol, transaction)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_TRANSACTION, 0u);
                break;
            }
            if (payload_length < 3u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
                break;
            }
            const uint16_t offset = read_u16_le(&request[8]);
            const uint8_t chunk_length = (uint8_t)(payload_length - 2u);
            if ((uint32_t)offset + chunk_length > protocol->staged_length) {
                make_response(response, command, transaction, HIDPILOT_STATUS_OFFSET, 0u);
                break;
            }
            for (uint16_t index = 0; index < chunk_length; ++index) {
                const uint16_t target = (uint16_t)(offset + index);
                const bool already_received = (protocol->staged_received[target / 8u] & (uint8_t)(1u << (target % 8u))) != 0u;
                if (already_received && protocol->staged_data[target] != request[10u + index]) {
                    make_response(response, command, transaction, HIDPILOT_STATUS_OFFSET, 0u);
                    return;
                }
            }
            memcpy(&protocol->staged_data[offset], &request[10], chunk_length);
            mark_received(protocol, offset, chunk_length);
            protocol->staged_valid = false;
            make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            break;
        }
        case HIDPILOT_CMD_STAGE_VALIDATE: {
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!same_stage_transaction(protocol, transaction)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_TRANSACTION, 0u);
            } else {
                make_response(response, command, transaction, validate_stage(protocol), 0u);
            }
            break;
        }
        case HIDPILOT_CMD_RUN_ONCE:
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!same_stage_transaction(protocol, transaction)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_TRANSACTION, 0u);
            } else if (!protocol->staged_valid) {
                make_response(response, command, transaction, HIDPILOT_STATUS_INCOMPLETE, 0u);
            } else if (!hidpilot_executor_run_once(protocol->executor, &protocol->staged_config, now_ms)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_BUSY, 0u);
            } else {
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            }
            break;
        case HIDPILOT_CMD_APPLY_TEMP:
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!same_stage_transaction(protocol, transaction)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_TRANSACTION, 0u);
            } else if (!protocol->staged_valid) {
                make_response(response, command, transaction, HIDPILOT_STATUS_INCOMPLETE, 0u);
            } else {
                *protocol->active_config = protocol->staged_config;
                hidpilot_executor_apply(protocol->executor, protocol->active_config, now_ms);
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            }
            break;
        case HIDPILOT_CMD_APPLY_SAVE:
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!same_stage_transaction(protocol, transaction)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_TRANSACTION, 0u);
            } else if (!protocol->staged_valid) {
                make_response(response, command, transaction, HIDPILOT_STATUS_INCOMPLETE, 0u);
            } else if (!protocol->io.save_config(protocol->io.context, &protocol->staged_config)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_FLASH, 0u);
            } else {
                *protocol->active_config = protocol->staged_config;
                hidpilot_executor_apply(protocol->executor, protocol->active_config, now_ms);
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            }
            break;
        case HIDPILOT_CMD_RESTORE_DEFAULT: {
            hidpilot_config_t defaults;
            hidpilot_config_default(&defaults);
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else if (!protocol->io.save_config(protocol->io.context, &defaults)) {
                make_response(response, command, transaction, HIDPILOT_STATUS_FLASH, 0u);
            } else {
                *protocol->active_config = defaults;
                hidpilot_executor_apply(protocol->executor, protocol->active_config, now_ms);
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
            }
            break;
        }
        case HIDPILOT_CMD_REBOOT_APPLICATION:
        case HIDPILOT_CMD_REBOOT_BOOTSEL:
            if (payload_length != 0u) {
                make_response(response, command, transaction, HIDPILOT_STATUS_LENGTH, 0u);
            } else {
                make_response(response, command, transaction, HIDPILOT_STATUS_OK, 0u);
                protocol->io.schedule_reboot(protocol->io.context, command == HIDPILOT_CMD_REBOOT_BOOTSEL);
            }
            break;
        default:
            make_response(response, command, transaction, HIDPILOT_STATUS_COMMAND, 0u);
            break;
    }
}
