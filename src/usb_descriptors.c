#include "usb_descriptors.h"

#include "pico/unique_id.h"
#include "tusb.h"

#include <stddef.h>
#include <string.h>

#define HIDPILOT_USB_VID 0xcafeu
#define HIDPILOT_USB_PID 0x4008u

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = HIDPILOT_USB_VID,
    .idProduct = HIDPILOT_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t input_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HIDPILOT_REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HIDPILOT_REPORT_ID_MOUSE)),
};

static const uint8_t config_report_descriptor[] = {
    HID_USAGE_PAGE_N(0xff00, 2),
    HID_USAGE(0x01),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_LOGICAL_MIN(0x00),
        HID_LOGICAL_MAX_N(0x00ff, 2),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(64),
        HID_USAGE(0x01),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_REPORT_COUNT(64),
        HID_USAGE(0x01),
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

enum {
    INTERFACE_INPUT,
    INTERFACE_CONFIG,
    INTERFACE_COUNT,
};

#define CONFIGURATION_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIGURATION_TOTAL_LENGTH, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(INTERFACE_INPUT, 4, HID_ITF_PROTOCOL_NONE, sizeof(input_report_descriptor), 0x81, 64, 1),
    TUD_HID_INOUT_DESCRIPTOR(INTERFACE_CONFIG, 5, HID_ITF_PROTOCOL_NONE, sizeof(config_report_descriptor), 0x02, 0x82, 64, 1),
};

static const char *const string_descriptors[] = {
    NULL,
    "HIDPilot",
    "HIDPilot XIAO RP2350",
    NULL,
    "Keyboard and Mouse",
    "HIDPilot Configuration",
};

static char serial_number[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2u + 1u];
static uint16_t string_descriptor[32 + 1];

void hidpilot_usb_init_serial(void) {
    pico_get_unique_board_id_string(serial_number, sizeof(serial_number));
}
const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == HIDPILOT_HID_INSTANCE_INPUT) {
        return input_report_descriptor;
    }
    if (instance == HIDPILOT_HID_INSTANCE_CONFIG) {
        return config_report_descriptor;
    }
    return NULL;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t character_count = 0u;
    if (index == 0u) {
        string_descriptor[1] = 0x0409;
        character_count = 1u;
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
            return NULL;
        }
        const char *source = index == 3u ? serial_number : string_descriptors[index];
        if (source == NULL) {
            return NULL;
        }
        character_count = strlen(source);
        const size_t maximum = sizeof(string_descriptor) / sizeof(string_descriptor[0]) - 1u;
        if (character_count > maximum) {
            character_count = maximum;
        }
        for (size_t character = 0; character < character_count; ++character) {
            string_descriptor[character + 1u] = (uint8_t)source[character];
        }
    }
    string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (uint16_t)(2u * character_count + 2u));
    return string_descriptor;
}
