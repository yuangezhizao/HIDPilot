#include "hidpilot_config.h"

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

void hidpilot_config_default(hidpilot_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->enabled = true;
    config->repeat_interval_ms = 55000u;
    config->action_count = 3u;
    config->actions[0].type = HIDPILOT_ACTION_MOUSE_MOVE;
    config->actions[0].value.move.x = 100;
    config->actions[1].type = HIDPILOT_ACTION_DELAY;
    config->actions[1].value.delay.duration_ms = 200u;
    config->actions[2].type = HIDPILOT_ACTION_MOUSE_MOVE;
    config->actions[2].value.move.x = -100;
}

hidpilot_config_result_t hidpilot_config_validate(const hidpilot_config_t *config) {
    if (config == NULL || config->action_count > HIDPILOT_MAX_ACTIONS) {
        return HIDPILOT_CONFIG_ERR_ACTION_COUNT;
    }
    if (config->repeat_interval_ms < HIDPILOT_REPEAT_MIN_MS || config->repeat_interval_ms > HIDPILOT_REPEAT_MAX_MS) {
        return HIDPILOT_CONFIG_ERR_INTERVAL;
    }
    for (uint8_t index = 0; index < config->action_count; ++index) {
        const hidpilot_action_t *action = &config->actions[index];
        switch (action->type) {
            case HIDPILOT_ACTION_DELAY:
                if (action->value.delay.duration_ms < 1u || action->value.delay.duration_ms > 60000u) {
                    return HIDPILOT_CONFIG_ERR_ACTION_VALUE;
                }
                break;
            case HIDPILOT_ACTION_MOUSE_MOVE:
                break;
            case HIDPILOT_ACTION_MOUSE_CLICK:
                if (action->value.mouse_click.buttons == 0u || (action->value.mouse_click.buttons & 0xe0u) != 0u ||
                    action->value.mouse_click.hold_ms < 10u || action->value.mouse_click.hold_ms > 1000u) {
                    return HIDPILOT_CONFIG_ERR_ACTION_VALUE;
                }
                break;
            case HIDPILOT_ACTION_KEYBOARD_CLICK:
                if (action->value.keyboard_click.usage == 0u || action->value.keyboard_click.hold_ms < 10u ||
                    action->value.keyboard_click.hold_ms > 1000u) {
                    return HIDPILOT_CONFIG_ERR_ACTION_VALUE;
                }
                break;
            default:
                return HIDPILOT_CONFIG_ERR_ACTION_TYPE;
        }
    }
    return HIDPILOT_CONFIG_OK;
}

hidpilot_config_result_t hidpilot_config_decode(hidpilot_config_t *config, const uint8_t *data, size_t length) {
    if (config == NULL || data == NULL || length < HIDPILOT_CONFIG_HEADER_SIZE) {
        return HIDPILOT_CONFIG_ERR_LENGTH;
    }
    if (data[0] != HIDPILOT_SCHEMA_VERSION) {
        return HIDPILOT_CONFIG_ERR_SCHEMA;
    }
    if ((data[1] & 0xfeu) != 0u) {
        return HIDPILOT_CONFIG_ERR_FLAGS;
    }
    if (data[3] != 0u || data[8] != 0u || data[9] != 0u || data[10] != 0u || data[11] != 0u) {
        return HIDPILOT_CONFIG_ERR_RESERVED;
    }
    const uint8_t action_count = data[2];
    if (action_count > HIDPILOT_MAX_ACTIONS) {
        return HIDPILOT_CONFIG_ERR_ACTION_COUNT;
    }
    const size_t expected_length = HIDPILOT_CONFIG_HEADER_SIZE + (size_t)action_count * HIDPILOT_ACTION_WIRE_SIZE;
    if (length != expected_length) {
        return HIDPILOT_CONFIG_ERR_LENGTH;
    }

    memset(config, 0, sizeof(*config));
    config->enabled = (data[1] & 1u) != 0u;
    config->action_count = action_count;
    config->repeat_interval_ms = read_u32_le(&data[4]);
    for (uint8_t index = 0; index < action_count; ++index) {
        const uint8_t *wire = &data[HIDPILOT_CONFIG_HEADER_SIZE + (size_t)index * HIDPILOT_ACTION_WIRE_SIZE];
        hidpilot_action_t *action = &config->actions[index];
        action->type = wire[0];
        switch (action->type) {
            case HIDPILOT_ACTION_DELAY:
                if (wire[1] != 0u || wire[2] != 0u || wire[3] != 0u) {
                    return HIDPILOT_CONFIG_ERR_RESERVED;
                }
                action->value.delay.duration_ms = read_u32_le(&wire[4]);
                break;
            case HIDPILOT_ACTION_MOUSE_MOVE:
                if (wire[5] != 0u || wire[6] != 0u || wire[7] != 0u) {
                    return HIDPILOT_CONFIG_ERR_RESERVED;
                }
                action->value.move.x = (int8_t)wire[1];
                action->value.move.y = (int8_t)wire[2];
                action->value.move.wheel = (int8_t)wire[3];
                action->value.move.pan = (int8_t)wire[4];
                break;
            case HIDPILOT_ACTION_MOUSE_CLICK:
                if (wire[4] != 0u || wire[5] != 0u || wire[6] != 0u || wire[7] != 0u) {
                    return HIDPILOT_CONFIG_ERR_RESERVED;
                }
                action->value.mouse_click.buttons = wire[1];
                action->value.mouse_click.hold_ms = read_u16_le(&wire[2]);
                break;
            case HIDPILOT_ACTION_KEYBOARD_CLICK:
                if (wire[3] != 0u || wire[6] != 0u || wire[7] != 0u) {
                    return HIDPILOT_CONFIG_ERR_RESERVED;
                }
                action->value.keyboard_click.modifiers = wire[1];
                action->value.keyboard_click.usage = wire[2];
                action->value.keyboard_click.hold_ms = read_u16_le(&wire[4]);
                break;
            default:
                return HIDPILOT_CONFIG_ERR_ACTION_TYPE;
        }
    }
    return hidpilot_config_validate(config);
}

size_t hidpilot_config_encode(const hidpilot_config_t *config, uint8_t *data, size_t capacity) {
    if (hidpilot_config_validate(config) != HIDPILOT_CONFIG_OK) {
        return 0u;
    }
    const size_t length = HIDPILOT_CONFIG_HEADER_SIZE + (size_t)config->action_count * HIDPILOT_ACTION_WIRE_SIZE;
    if (data == NULL || capacity < length) {
        return 0u;
    }
    memset(data, 0, length);
    data[0] = HIDPILOT_SCHEMA_VERSION;
    data[1] = config->enabled ? 1u : 0u;
    data[2] = config->action_count;
    write_u32_le(&data[4], config->repeat_interval_ms);
    for (uint8_t index = 0; index < config->action_count; ++index) {
        const hidpilot_action_t *action = &config->actions[index];
        uint8_t *wire = &data[HIDPILOT_CONFIG_HEADER_SIZE + (size_t)index * HIDPILOT_ACTION_WIRE_SIZE];
        wire[0] = action->type;
        switch (action->type) {
            case HIDPILOT_ACTION_DELAY:
                write_u32_le(&wire[4], action->value.delay.duration_ms);
                break;
            case HIDPILOT_ACTION_MOUSE_MOVE:
                wire[1] = (uint8_t)action->value.move.x;
                wire[2] = (uint8_t)action->value.move.y;
                wire[3] = (uint8_t)action->value.move.wheel;
                wire[4] = (uint8_t)action->value.move.pan;
                break;
            case HIDPILOT_ACTION_MOUSE_CLICK:
                wire[1] = action->value.mouse_click.buttons;
                write_u16_le(&wire[2], action->value.mouse_click.hold_ms);
                break;
            case HIDPILOT_ACTION_KEYBOARD_CLICK:
                wire[1] = action->value.keyboard_click.modifiers;
                wire[2] = action->value.keyboard_click.usage;
                write_u16_le(&wire[4], action->value.keyboard_click.hold_ms);
                break;
            default:
                return 0u;
        }
    }
    return length;
}

uint32_t hidpilot_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}
