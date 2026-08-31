#include "hidpilot_config.h"
#include "hidpilot_executor.h"
#include "hidpilot_flash_store.h"
#include "hidpilot_protocol.h"
#include "usb_descriptors.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static hidpilot_config_t active_config;
static hidpilot_executor_t executor;
static hidpilot_flash_store_t flash_store;
static hidpilot_protocol_t protocol;
static uint8_t pending_response[HIDPILOT_FRAME_SIZE];
static bool response_pending;
static bool reboot_pending;
static bool reboot_to_bootsel;
static uint32_t reboot_deadline_ms;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool send_keyboard(void *context, uint8_t modifiers, uint8_t usage) {
    (void)context;
    if (!tud_hid_n_ready(HIDPILOT_HID_INSTANCE_INPUT)) {
        return false;
    }
    uint8_t keycodes[6] = {0};
    keycodes[0] = usage;
    return tud_hid_n_keyboard_report(HIDPILOT_HID_INSTANCE_INPUT, HIDPILOT_REPORT_ID_KEYBOARD, modifiers, keycodes);
}

static bool send_mouse(void *context, uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
    (void)context;
    if (!tud_hid_n_ready(HIDPILOT_HID_INSTANCE_INPUT)) {
        return false;
    }
    return tud_hid_n_mouse_report(HIDPILOT_HID_INSTANCE_INPUT, HIDPILOT_REPORT_ID_MOUSE, buttons, x, y, wheel, pan);
}

static void request_remote_wakeup(void *context) {
    (void)context;
    tud_remote_wakeup();
}

static bool save_config(void *context, const hidpilot_config_t *config) {
    (void)context;
    const bool saved = hidpilot_flash_store_save(&flash_store, config);
    if (saved) {
        hidpilot_protocol_set_storage_status(&protocol, flash_store.active_slot, flash_store.generation);
    }
    return saved;
}

static void schedule_reboot(void *context, bool bootsel) {
    (void)context;
    reboot_pending = true;
    reboot_to_bootsel = bootsel;
    reboot_deadline_ms = now_ms() + 100u;
}

int main(void) {
    stdio_init_all();
    hidpilot_flash_store_load(&flash_store, &active_config);
    const hidpilot_executor_io_t executor_io = {
        .context = NULL,
        .send_keyboard = send_keyboard,
        .send_mouse = send_mouse,
        .request_remote_wakeup = request_remote_wakeup,
    };
    hidpilot_executor_init(&executor, &active_config, executor_io);
    const hidpilot_protocol_io_t protocol_io = {
        .context = NULL,
        .save_config = save_config,
        .schedule_reboot = schedule_reboot,
    };
    hidpilot_protocol_init(&protocol, &active_config, &executor, protocol_io);
    hidpilot_protocol_set_storage_status(&protocol, flash_store.active_slot, flash_store.generation);
    hidpilot_usb_init_serial();
    tusb_init();

    while (true) {
        tud_task();
        const uint32_t current_ms = now_ms();
        hidpilot_executor_tick(&executor, current_ms);
        if (response_pending && tud_hid_n_ready(HIDPILOT_HID_INSTANCE_CONFIG) &&
            tud_hid_n_report(HIDPILOT_HID_INSTANCE_CONFIG, 0u, pending_response, sizeof(pending_response))) {
            response_pending = false;
        }
        if (reboot_pending && (int32_t)(current_ms - reboot_deadline_ms) >= 0) {
            if (reboot_to_bootsel) {
                reset_usb_boot(0u, 0u);
            }
            watchdog_reboot(0u, 0u, 0u);
            while (true) {
                tight_loop_contents();
            }
        }
        tight_loop_contents();
    }
}

void tud_mount_cb(void) {
    hidpilot_executor_mount(&executor, now_ms());
}

void tud_umount_cb(void) {
    hidpilot_executor_unmount(&executor);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    hidpilot_executor_suspend(&executor, remote_wakeup_en);
}

void tud_resume_cb(void) {
    hidpilot_executor_resume(&executor, now_ms());
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t requested_length) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t buffer_size) {
    (void)report_id;
    if (instance != HIDPILOT_HID_INSTANCE_CONFIG || report_type != HID_REPORT_TYPE_OUTPUT || response_pending) {
        return;
    }
    uint8_t request[HIDPILOT_FRAME_SIZE] = {0};
    const uint16_t copy_length = buffer_size < HIDPILOT_FRAME_SIZE ? buffer_size : HIDPILOT_FRAME_SIZE;
    memcpy(request, buffer, copy_length);
    if (buffer_size != HIDPILOT_FRAME_SIZE) {
        request[6] = 0xffu;
    }
    hidpilot_protocol_handle(&protocol, request, pending_response, now_ms());
    response_pending = true;
}
