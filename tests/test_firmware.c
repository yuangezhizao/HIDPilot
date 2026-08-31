#include "hidpilot_config.h"
#include "hidpilot_executor.h"
#include "hidpilot_flash_store.h"
#include "hidpilot_protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t keyboard_count;
    uint32_t mouse_count;
    uint32_t wake_count;
    uint8_t last_modifiers;
    uint8_t last_usage;
    uint8_t last_buttons;
    int8_t last_x;
    bool allow_send;
} fake_hid_t;

static bool fake_keyboard(void *context, uint8_t modifiers, uint8_t usage) {
    fake_hid_t *fake = context;
    if (!fake->allow_send) return false;
    ++fake->keyboard_count;
    fake->last_modifiers = modifiers;
    fake->last_usage = usage;
    return true;
}

static bool fake_mouse(void *context, uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
    fake_hid_t *fake = context;
    (void)y;
    (void)wheel;
    (void)pan;
    if (!fake->allow_send) return false;
    ++fake->mouse_count;
    fake->last_buttons = buttons;
    fake->last_x = x;
    return true;
}

static void fake_wakeup(void *context) {
    ++((fake_hid_t *)context)->wake_count;
}

static hidpilot_executor_t make_executor(hidpilot_config_t *config, fake_hid_t *fake) {
    const hidpilot_executor_io_t io = {
        .context = fake,
        .send_keyboard = fake_keyboard,
        .send_mouse = fake_mouse,
        .request_remote_wakeup = fake_wakeup,
    };
    hidpilot_executor_t executor;
    hidpilot_executor_init(&executor, config, io);
    return executor;
}

static void test_config(void) {
    hidpilot_config_t config;
    hidpilot_config_default(&config);
    assert(config.enabled);
    assert(config.repeat_interval_ms == 55000u);
    assert(config.action_count == 3u);
    assert(config.actions[0].value.move.x == 100);
    assert(config.actions[1].value.delay.duration_ms == 200u);
    assert(config.actions[2].value.move.x == -100);

    uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
    const size_t length = hidpilot_config_encode(&config, encoded, sizeof(encoded));
    assert(length == 36u);
    assert(hidpilot_crc32((const uint8_t *)"123456789", 9u) == 0xcbf43926u);
    hidpilot_config_t decoded;
    assert(hidpilot_config_decode(&decoded, encoded, length) == HIDPILOT_CONFIG_OK);
    assert(memcmp(encoded, (uint8_t[HIDPILOT_CONFIG_MAX_SIZE]){0}, 1u) != 0);

    config.action_count = 0u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_OK);
    config.repeat_interval_ms = 0u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_ERR_INTERVAL);
    config.repeat_interval_ms = HIDPILOT_REPEAT_MAX_MS;
    config.action_count = 1u;
    config.actions[0].type = HIDPILOT_ACTION_DELAY;
    config.actions[0].value.delay.duration_ms = 1u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_OK);
    config.actions[0].value.delay.duration_ms = 60001u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_ERR_ACTION_VALUE);

    memset(&config, 0, sizeof(config));
    config.enabled = true;
    config.repeat_interval_ms = HIDPILOT_REPEAT_MIN_MS;
    config.action_count = 4u;
    config.actions[0].type = HIDPILOT_ACTION_DELAY;
    config.actions[0].value.delay.duration_ms = 60000u;
    config.actions[1].type = HIDPILOT_ACTION_MOUSE_MOVE;
    config.actions[1].value.move.x = -127;
    config.actions[1].value.move.y = 127;
    config.actions[1].value.move.wheel = -127;
    config.actions[1].value.move.pan = 127;
    config.actions[2].type = HIDPILOT_ACTION_MOUSE_CLICK;
    config.actions[2].value.mouse_click.buttons = 31u;
    config.actions[2].value.mouse_click.hold_ms = 1000u;
    config.actions[3].type = HIDPILOT_ACTION_KEYBOARD_CLICK;
    config.actions[3].value.keyboard_click.modifiers = 0xffu;
    config.actions[3].value.keyboard_click.usage = 0xffu;
    config.actions[3].value.keyboard_click.hold_ms = 10u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_OK);
    config.actions[2].value.mouse_click.buttons = 32u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_ERR_ACTION_VALUE);
    config.actions[2].value.mouse_click.buttons = 1u;
    config.actions[3].value.keyboard_click.usage = 0u;
    assert(hidpilot_config_validate(&config) == HIDPILOT_CONFIG_ERR_ACTION_VALUE);

    encoded[3] = 1u;
    assert(hidpilot_config_decode(&decoded, encoded, length) == HIDPILOT_CONFIG_ERR_RESERVED);
    encoded[3] = 0u;
    encoded[0] = 2u;
    assert(hidpilot_config_decode(&decoded, encoded, length) == HIDPILOT_CONFIG_ERR_SCHEMA);
}

static void finish_default_run(hidpilot_executor_t *executor) {
    hidpilot_executor_tick(executor, 0u);
    hidpilot_executor_tick(executor, 1u);
    hidpilot_executor_tick(executor, 2u);
    hidpilot_executor_tick(executor, 3u);
    hidpilot_executor_tick(executor, 203u);
    hidpilot_executor_tick(executor, 204u);
    hidpilot_executor_tick(executor, 205u);
}

static void test_executor(void) {
    hidpilot_config_t config;
    hidpilot_config_default(&config);
    fake_hid_t fake = {.allow_send = true};
    hidpilot_executor_t executor = make_executor(&config, &fake);
    hidpilot_executor_mount(&executor, 0u);
    finish_default_run(&executor);
    assert(executor.completed_runs == 1u);
    assert(fake.last_x == -100);
    assert(executor.next_cycle_ms == 55001u);

    hidpilot_executor_tick(&executor, 206u);
    hidpilot_executor_tick(&executor, 207u);
    hidpilot_executor_tick(&executor, 55000u);
    assert(executor.completed_runs == 1u);
    hidpilot_executor_tick(&executor, 55001u);
    assert(executor.state == HIDPILOT_EXECUTOR_ACTION);

    hidpilot_executor_suspend(&executor, true);
    hidpilot_executor_tick(&executor, 55002u);
    assert(fake.wake_count == 1u);
    hidpilot_executor_tick(&executor, 56000u);
    assert(fake.wake_count == 1u);
    hidpilot_executor_resume(&executor, 56000u);
    const uint32_t keyboard_before_resume = fake.keyboard_count;
    const uint32_t mouse_before_resume = fake.mouse_count;
    hidpilot_executor_tick(&executor, 56000u);
    hidpilot_executor_tick(&executor, 56001u);
    assert(fake.keyboard_count == keyboard_before_resume + 1u);
    assert(fake.mouse_count == mouse_before_resume + 1u);
    assert(fake.last_usage == 0u && fake.last_buttons == 0u);
    assert(executor.state == HIDPILOT_EXECUTOR_ACTION);

    hidpilot_config_t long_config;
    memset(&long_config, 0, sizeof(long_config));
    long_config.enabled = true;
    long_config.repeat_interval_ms = 10u;
    long_config.action_count = 1u;
    long_config.actions[0].type = HIDPILOT_ACTION_DELAY;
    long_config.actions[0].value.delay.duration_ms = 100u;
    hidpilot_executor_apply(&executor, &long_config, 1000u);
    hidpilot_executor_tick(&executor, 1000u);
    hidpilot_executor_tick(&executor, 1001u);
    hidpilot_executor_tick(&executor, 1002u);
    hidpilot_executor_tick(&executor, 1102u);
    hidpilot_executor_tick(&executor, 1103u);
    assert(executor.completed_runs >= 2u);
    hidpilot_executor_tick(&executor, 1104u);
    hidpilot_executor_tick(&executor, 1105u);
    assert(executor.state == HIDPILOT_EXECUTOR_ACTION);

    hidpilot_config_t click_config;
    memset(&click_config, 0, sizeof(click_config));
    click_config.enabled = false;
    click_config.repeat_interval_ms = 1000u;
    click_config.action_count = 2u;
    click_config.actions[0].type = HIDPILOT_ACTION_MOUSE_CLICK;
    click_config.actions[0].value.mouse_click.buttons = 1u;
    click_config.actions[0].value.mouse_click.hold_ms = 10u;
    click_config.actions[1].type = HIDPILOT_ACTION_KEYBOARD_CLICK;
    click_config.actions[1].value.keyboard_click.modifiers = 2u;
    click_config.actions[1].value.keyboard_click.usage = 4u;
    click_config.actions[1].value.keyboard_click.hold_ms = 10u;
    hidpilot_executor_apply(&executor, &click_config, 2000u);
    hidpilot_executor_tick(&executor, 2000u);
    hidpilot_executor_tick(&executor, 2001u);
    assert(!hidpilot_executor_busy(&executor));
    assert(hidpilot_executor_run_once(&executor, &click_config, 2002u));
    hidpilot_executor_tick(&executor, 2002u);
    hidpilot_executor_tick(&executor, 2003u);
    hidpilot_executor_tick(&executor, 2004u);
    assert(fake.last_buttons == 1u);
    hidpilot_executor_tick(&executor, 2014u);
    assert(fake.last_buttons == 0u);
    hidpilot_executor_tick(&executor, 2015u);
    assert(fake.last_usage == 4u);
    hidpilot_executor_tick(&executor, 2025u);
    assert(fake.last_usage == 0u && fake.last_modifiers == 0u);
}

static void test_flash_records(void) {
    uint8_t slot0[HIDPILOT_FLASH_SECTOR_SIZE];
    uint8_t slot1[HIDPILOT_FLASH_SECTOR_SIZE];
    hidpilot_config_t config;
    hidpilot_config_default(&config);
    assert(hidpilot_store_build_record(&config, 7u, slot0, sizeof(slot0)));
    config.repeat_interval_ms = 1234u;
    assert(hidpilot_store_build_record(&config, 8u, slot1, sizeof(slot1)));
    hidpilot_config_t selected;
    uint32_t generation;
    assert(hidpilot_store_select_records(slot0, slot1, &selected, &generation) == 1);
    assert(generation == 8u && selected.repeat_interval_ms == 1234u);

    slot1[HIDPILOT_STORE_HEADER_SIZE + 5u] ^= 0x40u;
    assert(hidpilot_store_select_records(slot0, slot1, &selected, &generation) == 0);
    assert(generation == 7u);

    memset(slot0, 0xff, sizeof(slot0));
    memset(slot1, 0xff, sizeof(slot1));
    assert(hidpilot_store_select_records(slot0, slot1, &selected, &generation) == -1);

    hidpilot_config_default(&config);
    assert(hidpilot_store_build_record(&config, UINT32_MAX, slot0, sizeof(slot0)));
    config.repeat_interval_ms = 42u;
    assert(hidpilot_store_build_record(&config, 0u, slot1, sizeof(slot1)));
    assert(hidpilot_store_select_records(slot0, slot1, &selected, &generation) == 1);
    assert(generation == 0u && selected.repeat_interval_ms == 42u);

    memset(slot1 + 24u, 0xff, sizeof(slot1) - 24u);
    assert(hidpilot_store_select_records(slot0, slot1, &selected, &generation) == 0);
}

typedef struct {
    bool save_ok;
    bool rebooted;
    bool bootsel;
} fake_protocol_io_t;

static bool fake_save(void *context, const hidpilot_config_t *config) {
    (void)config;
    return ((fake_protocol_io_t *)context)->save_ok;
}

static void fake_reboot(void *context, bool bootsel) {
    fake_protocol_io_t *fake = context;
    fake->rebooted = true;
    fake->bootsel = bootsel;
}

static void make_frame(uint8_t frame[HIDPILOT_FRAME_SIZE], uint8_t command, uint16_t transaction,
                       const uint8_t *payload, uint8_t payload_length) {
    memset(frame, 0, HIDPILOT_FRAME_SIZE);
    frame[0] = 'H';
    frame[1] = 'P';
    frame[2] = HIDPILOT_PROTOCOL_VERSION;
    frame[3] = command;
    frame[4] = (uint8_t)transaction;
    frame[5] = (uint8_t)(transaction >> 8u);
    frame[6] = payload_length;
    if (payload_length > 0u) memcpy(&frame[8], payload, payload_length);
}

static uint8_t request_status(hidpilot_protocol_t *protocol, uint8_t command, uint16_t transaction,
                              const uint8_t *payload, uint8_t payload_length) {
    uint8_t request[HIDPILOT_FRAME_SIZE];
    uint8_t response[HIDPILOT_FRAME_SIZE];
    make_frame(request, command, transaction, payload, payload_length);
    hidpilot_protocol_handle(protocol, request, response, 100u);
    assert(response[4] == (uint8_t)transaction && response[5] == (uint8_t)(transaction >> 8u));
    return response[7];
}

static void test_protocol(void) {
    hidpilot_config_t config;
    hidpilot_config_default(&config);
    fake_hid_t fake_hid = {.allow_send = true};
    hidpilot_executor_t executor = make_executor(&config, &fake_hid);
    fake_protocol_io_t fake_io = {.save_ok = true};
    const hidpilot_protocol_io_t io = {.context = &fake_io, .save_config = fake_save, .schedule_reboot = fake_reboot};
    hidpilot_protocol_t protocol;
    hidpilot_protocol_init(&protocol, &config, &executor, io);

    uint8_t encoded[HIDPILOT_CONFIG_MAX_SIZE];
    const size_t length = hidpilot_config_encode(&config, encoded, sizeof(encoded));
    const uint32_t crc = hidpilot_crc32(encoded, length);
    uint8_t begin[6] = {(uint8_t)length, (uint8_t)(length >> 8u), (uint8_t)crc, (uint8_t)(crc >> 8u), (uint8_t)(crc >> 16u), (uint8_t)(crc >> 24u)};
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_BEGIN, 55u, begin, sizeof(begin)) == HIDPILOT_STATUS_OK);

    uint8_t chunk[56];
    chunk[0] = 20u;
    chunk[1] = 0u;
    memcpy(&chunk[2], &encoded[20], length - 20u);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 55u, chunk, (uint8_t)(length - 18u)) == HIDPILOT_STATUS_OK);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_VALIDATE, 55u, NULL, 0u) == HIDPILOT_STATUS_INCOMPLETE);
    chunk[0] = 0u;
    chunk[1] = 0u;
    memcpy(&chunk[2], encoded, 20u);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 55u, chunk, 22u) == HIDPILOT_STATUS_OK);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 55u, chunk, 22u) == HIDPILOT_STATUS_OK);
    chunk[2] ^= 1u;
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 55u, chunk, 22u) == HIDPILOT_STATUS_OFFSET);
    chunk[2] ^= 1u;
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_VALIDATE, 55u, NULL, 0u) == HIDPILOT_STATUS_OK);
    assert(request_status(&protocol, HIDPILOT_CMD_APPLY_TEMP, 55u, NULL, 0u) == HIDPILOT_STATUS_OK);

    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 99u, chunk, 22u) == HIDPILOT_STATUS_TRANSACTION);
    chunk[0] = 250u;
    chunk[1] = 0u;
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 55u, chunk, 22u) == HIDPILOT_STATUS_OFFSET);
    assert(request_status(&protocol, 0x7fu, 1u, NULL, 0u) == HIDPILOT_STATUS_COMMAND);

    uint8_t request[HIDPILOT_FRAME_SIZE];
    uint8_t response[HIDPILOT_FRAME_SIZE];
    make_frame(request, HIDPILOT_CMD_GET_STATUS, 3u, NULL, 0u);
    request[2] = 2u;
    hidpilot_protocol_handle(&protocol, request, response, 0u);
    assert(response[7] == HIDPILOT_STATUS_VERSION);
    request[0] = 'X';
    hidpilot_protocol_handle(&protocol, request, response, 0u);
    assert(response[7] == HIDPILOT_STATUS_MAGIC);

    encoded[5] ^= 1u;
    const uint32_t wrong_crc = crc;
    begin[0] = (uint8_t)length;
    begin[1] = (uint8_t)(length >> 8u);
    begin[2] = (uint8_t)wrong_crc;
    begin[3] = (uint8_t)(wrong_crc >> 8u);
    begin[4] = (uint8_t)(wrong_crc >> 16u);
    begin[5] = (uint8_t)(wrong_crc >> 24u);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_BEGIN, 77u, begin, 6u) == HIDPILOT_STATUS_OK);
    chunk[0] = 0u;
    chunk[1] = 0u;
    memcpy(&chunk[2], encoded, length);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 77u, chunk, (uint8_t)(length + 2u)) == HIDPILOT_STATUS_OK);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_VALIDATE, 77u, NULL, 0u) == HIDPILOT_STATUS_CRC);

    hidpilot_config_default(&config);
    const size_t invalid_length = hidpilot_config_encode(&config, encoded, sizeof(encoded));
    encoded[3] = 1u;
    const uint32_t invalid_crc = hidpilot_crc32(encoded, invalid_length);
    begin[0] = (uint8_t)invalid_length;
    begin[1] = (uint8_t)(invalid_length >> 8u);
    begin[2] = (uint8_t)invalid_crc;
    begin[3] = (uint8_t)(invalid_crc >> 8u);
    begin[4] = (uint8_t)(invalid_crc >> 16u);
    begin[5] = (uint8_t)(invalid_crc >> 24u);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_BEGIN, 88u, begin, 6u) == HIDPILOT_STATUS_OK);
    chunk[0] = 0u;
    chunk[1] = 0u;
    memcpy(&chunk[2], encoded, invalid_length);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_CHUNK, 88u, chunk, (uint8_t)(invalid_length + 2u)) == HIDPILOT_STATUS_OK);
    assert(request_status(&protocol, HIDPILOT_CMD_STAGE_VALIDATE, 88u, NULL, 0u) == HIDPILOT_STATUS_CONFIG);

    assert(request_status(&protocol, HIDPILOT_CMD_REBOOT_BOOTSEL, 5u, NULL, 0u) == HIDPILOT_STATUS_OK);
    assert(fake_io.rebooted && fake_io.bootsel);
}

int main(void) {
    test_config();
    test_executor();
    test_flash_records();
    test_protocol();
    puts("firmware host tests: PASS");
    return 0;
}
