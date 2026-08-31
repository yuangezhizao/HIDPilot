#ifndef HIDPILOT_CONFIG_H
#define HIDPILOT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIDPILOT_SCHEMA_VERSION 1u
#define HIDPILOT_MAX_ACTIONS 32u
#define HIDPILOT_CONFIG_HEADER_SIZE 12u
#define HIDPILOT_ACTION_WIRE_SIZE 8u
#define HIDPILOT_CONFIG_MAX_SIZE (HIDPILOT_CONFIG_HEADER_SIZE + HIDPILOT_MAX_ACTIONS * HIDPILOT_ACTION_WIRE_SIZE)
#define HIDPILOT_REPEAT_MIN_MS 1u
#define HIDPILOT_REPEAT_MAX_MS 86400000u

typedef enum {
    HIDPILOT_ACTION_DELAY = 1,
    HIDPILOT_ACTION_MOUSE_MOVE = 2,
    HIDPILOT_ACTION_MOUSE_CLICK = 3,
    HIDPILOT_ACTION_KEYBOARD_CLICK = 4,
} hidpilot_action_type_t;

typedef struct {
    uint8_t type;
    union {
        struct {
            uint32_t duration_ms;
        } delay;
        struct {
            int8_t x;
            int8_t y;
            int8_t wheel;
            int8_t pan;
        } move;
        struct {
            uint8_t buttons;
            uint16_t hold_ms;
        } mouse_click;
        struct {
            uint8_t modifiers;
            uint8_t usage;
            uint16_t hold_ms;
        } keyboard_click;
    } value;
} hidpilot_action_t;

typedef struct {
    bool enabled;
    uint32_t repeat_interval_ms;
    uint8_t action_count;
    hidpilot_action_t actions[HIDPILOT_MAX_ACTIONS];
} hidpilot_config_t;

typedef enum {
    HIDPILOT_CONFIG_OK = 0,
    HIDPILOT_CONFIG_ERR_LENGTH,
    HIDPILOT_CONFIG_ERR_SCHEMA,
    HIDPILOT_CONFIG_ERR_FLAGS,
    HIDPILOT_CONFIG_ERR_RESERVED,
    HIDPILOT_CONFIG_ERR_ACTION_COUNT,
    HIDPILOT_CONFIG_ERR_INTERVAL,
    HIDPILOT_CONFIG_ERR_ACTION_TYPE,
    HIDPILOT_CONFIG_ERR_ACTION_VALUE,
} hidpilot_config_result_t;

void hidpilot_config_default(hidpilot_config_t *config);
hidpilot_config_result_t hidpilot_config_validate(const hidpilot_config_t *config);
hidpilot_config_result_t hidpilot_config_decode(hidpilot_config_t *config, const uint8_t *data, size_t length);
size_t hidpilot_config_encode(const hidpilot_config_t *config, uint8_t *data, size_t capacity);
uint32_t hidpilot_crc32(const uint8_t *data, size_t length);

#endif
