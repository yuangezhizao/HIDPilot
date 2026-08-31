#ifndef HIDPILOT_EXECUTOR_H
#define HIDPILOT_EXECUTOR_H

#include "hidpilot_config.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *context;
    bool (*send_keyboard)(void *context, uint8_t modifiers, uint8_t usage);
    bool (*send_mouse)(void *context, uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);
    void (*request_remote_wakeup)(void *context);
} hidpilot_executor_io_t;

typedef enum {
    HIDPILOT_EXECUTOR_DETACHED = 0,
    HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD,
    HIDPILOT_EXECUTOR_NEUTRAL_MOUSE,
    HIDPILOT_EXECUTOR_WAITING,
    HIDPILOT_EXECUTOR_ACTION,
    HIDPILOT_EXECUTOR_MOUSE_MOVE,
    HIDPILOT_EXECUTOR_DELAY,
    HIDPILOT_EXECUTOR_MOUSE_RELEASE,
    HIDPILOT_EXECUTOR_KEYBOARD_RELEASE,
} hidpilot_executor_state_t;

typedef struct {
    hidpilot_config_t active_config;
    hidpilot_config_t run_config;
    hidpilot_executor_io_t io;
    hidpilot_executor_state_t state;
    uint32_t deadline_ms;
    uint32_t next_cycle_ms;
    uint32_t completed_runs;
    uint32_t error_count;
    uint32_t move_start_ms;
    int16_t move_sent_x;
    int16_t move_sent_y;
    int16_t move_sent_wheel;
    int16_t move_sent_pan;
    uint16_t move_duration_ms;
    uint16_t move_step_count;
    uint16_t move_step_index;
    uint8_t action_index;
    int16_t move_target_x;
    int16_t move_target_y;
    int8_t move_target_wheel;
    int8_t move_target_pan;
    bool mounted;
    bool suspended;
    bool remote_wakeup_allowed;
    bool wake_requested;
    bool pending_run;
    bool one_shot;
    bool activity_active;
} hidpilot_executor_t;

void hidpilot_executor_init(hidpilot_executor_t *executor, const hidpilot_config_t *config, hidpilot_executor_io_t io);
void hidpilot_executor_apply(hidpilot_executor_t *executor, const hidpilot_config_t *config, uint32_t now_ms);
bool hidpilot_executor_run_once(hidpilot_executor_t *executor, const hidpilot_config_t *config, uint32_t now_ms);
void hidpilot_executor_mount(hidpilot_executor_t *executor, uint32_t now_ms);
void hidpilot_executor_unmount(hidpilot_executor_t *executor);
void hidpilot_executor_suspend(hidpilot_executor_t *executor, bool remote_wakeup_allowed);
void hidpilot_executor_resume(hidpilot_executor_t *executor, uint32_t now_ms);
void hidpilot_executor_tick(hidpilot_executor_t *executor, uint32_t now_ms);
bool hidpilot_executor_busy(const hidpilot_executor_t *executor);
bool hidpilot_executor_activity_active(const hidpilot_executor_t *executor);

#endif
