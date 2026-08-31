#ifndef HIDPILOT_USB_DESCRIPTORS_H
#define HIDPILOT_USB_DESCRIPTORS_H

#include <stdint.h>

enum {
    HIDPILOT_REPORT_ID_KEYBOARD = 1,
    HIDPILOT_REPORT_ID_MOUSE = 2,
};

enum {
    HIDPILOT_HID_INSTANCE_INPUT = 0,
    HIDPILOT_HID_INSTANCE_CONFIG = 1,
};

void hidpilot_usb_init_serial(void);

#endif
