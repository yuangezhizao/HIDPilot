#include "hidpilot_executor.h"

#include <string.h>

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void begin_neutral_release(hidpilot_executor_t *executor, bool pending_run) {
    executor->state = executor->mounted && !executor->suspended ? HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD : HIDPILOT_EXECUTOR_DETACHED;
    executor->pending_run = pending_run;
    executor->action_index = 0u;
    executor->activity_active = false;
}

void hidpilot_executor_init(hidpilot_executor_t *executor, const hidpilot_config_t *config, hidpilot_executor_io_t io) {
    memset(executor, 0, sizeof(*executor));
    executor->active_config = *config;
    executor->run_config = *config;
    executor->io = io;
    executor->state = HIDPILOT_EXECUTOR_DETACHED;
}

void hidpilot_executor_apply(hidpilot_executor_t *executor, const hidpilot_config_t *config, uint32_t now_ms) {
    executor->active_config = *config;
    executor->run_config = *config;
    executor->one_shot = false;
    executor->next_cycle_ms = now_ms;
    begin_neutral_release(executor, config->enabled);
}

bool hidpilot_executor_run_once(hidpilot_executor_t *executor, const hidpilot_config_t *config, uint32_t now_ms) {
    if (hidpilot_executor_busy(executor)) {
        return false;
    }
    executor->run_config = *config;
    executor->one_shot = true;
    executor->next_cycle_ms = now_ms;
    begin_neutral_release(executor, true);
    return true;
}

void hidpilot_executor_mount(hidpilot_executor_t *executor, uint32_t now_ms) {
    executor->mounted = true;
    executor->suspended = false;
    executor->wake_requested = false;
    executor->run_config = executor->active_config;
    executor->one_shot = false;
    executor->next_cycle_ms = now_ms;
    begin_neutral_release(executor, executor->active_config.enabled);
}

void hidpilot_executor_unmount(hidpilot_executor_t *executor) {
    executor->mounted = false;
    executor->suspended = false;
    executor->wake_requested = false;
    executor->pending_run = false;
    executor->one_shot = false;
    executor->state = HIDPILOT_EXECUTOR_DETACHED;
    executor->action_index = 0u;
    executor->activity_active = false;
}

void hidpilot_executor_suspend(hidpilot_executor_t *executor, bool remote_wakeup_allowed) {
    executor->suspended = true;
    executor->remote_wakeup_allowed = remote_wakeup_allowed;
    executor->wake_requested = false;
    executor->pending_run = executor->pending_run || executor->state != HIDPILOT_EXECUTOR_WAITING;
    executor->state = HIDPILOT_EXECUTOR_DETACHED;
    executor->action_index = 0u;
    executor->activity_active = false;
}

void hidpilot_executor_resume(hidpilot_executor_t *executor, uint32_t now_ms) {
    executor->suspended = false;
    executor->wake_requested = false;
    if (!executor->pending_run && executor->active_config.enabled && time_reached(now_ms, executor->next_cycle_ms)) {
        executor->pending_run = true;
    }
    executor->state = HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD;
}

static void complete_run(hidpilot_executor_t *executor, uint32_t now_ms) {
    ++executor->completed_runs;
    if (executor->one_shot) {
        executor->one_shot = false;
        executor->run_config = executor->active_config;
        executor->next_cycle_ms = now_ms + executor->active_config.repeat_interval_ms;
        executor->pending_run = false;
    } else {
        executor->pending_run = executor->active_config.enabled && time_reached(now_ms, executor->next_cycle_ms);
    }
    executor->state = HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD;
}

static void start_cycle(hidpilot_executor_t *executor, uint32_t now_ms) {
    executor->pending_run = false;
    executor->action_index = 0u;
    if (!executor->one_shot) {
        executor->next_cycle_ms = now_ms + executor->run_config.repeat_interval_ms;
    }
    executor->activity_active = true;
    executor->state = HIDPILOT_EXECUTOR_ACTION;
}

static uint16_t move_magnitude(int16_t value) {
    return (uint16_t)(value < 0 ? -value : value);
}

static void begin_mouse_move(hidpilot_executor_t *executor, const hidpilot_action_t *action, uint32_t now_ms) {
    uint16_t maximum = move_magnitude(action->value.move.x);
    const uint16_t y = move_magnitude(action->value.move.y);
    const uint16_t wheel = move_magnitude(action->value.move.wheel);
    const uint16_t pan = move_magnitude(action->value.move.pan);
    if (y > maximum) maximum = y;
    if (wheel > maximum) maximum = wheel;
    if (pan > maximum) maximum = pan;
    if (maximum == 0u) {
        executor->deadline_ms = now_ms + action->value.move.duration_ms;
        ++executor->action_index;
        executor->state = action->value.move.duration_ms == 0u ? HIDPILOT_EXECUTOR_ACTION : HIDPILOT_EXECUTOR_DELAY;
        return;
    }

    executor->move_start_ms = now_ms;
    executor->move_duration_ms = action->value.move.duration_ms;
    const uint16_t minimum_steps = (uint16_t)((maximum + 126u) / 127u);
    const uint16_t timed_steps = maximum < action->value.move.duration_ms ? maximum : action->value.move.duration_ms;
    executor->move_step_count = minimum_steps > timed_steps ? minimum_steps : timed_steps;
    executor->move_step_index = 0u;
    executor->move_sent_x = 0;
    executor->move_sent_y = 0;
    executor->move_sent_wheel = 0;
    executor->move_sent_pan = 0;
    executor->move_target_x = action->value.move.x;
    executor->move_target_y = action->value.move.y;
    executor->move_target_wheel = action->value.move.wheel;
    executor->move_target_pan = action->value.move.pan;
    ++executor->action_index;
    executor->state = HIDPILOT_EXECUTOR_MOUSE_MOVE;
}

static void run_mouse_move(hidpilot_executor_t *executor, uint32_t now_ms) {
    const uint16_t next_step = (uint16_t)(executor->move_step_index + 1u);
    const uint32_t offset_ms = ((uint32_t)next_step * executor->move_duration_ms + executor->move_step_count - 1u) /
                               executor->move_step_count;
    if (!time_reached(now_ms, executor->move_start_ms + offset_ms)) {
        return;
    }

    const int16_t target_x = (int16_t)(((int32_t)executor->move_target_x * next_step) / executor->move_step_count);
    const int16_t target_y = (int16_t)(((int32_t)executor->move_target_y * next_step) / executor->move_step_count);
    const int16_t target_wheel = (int16_t)(((int32_t)executor->move_target_wheel * next_step) / executor->move_step_count);
    const int16_t target_pan = (int16_t)(((int32_t)executor->move_target_pan * next_step) / executor->move_step_count);
    const int8_t delta_x = (int8_t)(target_x - executor->move_sent_x);
    const int8_t delta_y = (int8_t)(target_y - executor->move_sent_y);
    const int8_t delta_wheel = (int8_t)(target_wheel - executor->move_sent_wheel);
    const int8_t delta_pan = (int8_t)(target_pan - executor->move_sent_pan);
    if (!executor->io.send_mouse(executor->io.context, 0u, delta_x, delta_y, delta_wheel, delta_pan)) {
        return;
    }

    executor->move_sent_x = target_x;
    executor->move_sent_y = target_y;
    executor->move_sent_wheel = target_wheel;
    executor->move_sent_pan = target_pan;
    executor->move_step_index = next_step;
    if (next_step == executor->move_step_count) {
        executor->state = HIDPILOT_EXECUTOR_ACTION;
    }
}

static void run_action(hidpilot_executor_t *executor, uint32_t now_ms) {
    if (executor->action_index >= executor->run_config.action_count) {
        complete_run(executor, now_ms);
        return;
    }
    const hidpilot_action_t *action = &executor->run_config.actions[executor->action_index];
    switch (action->type) {
        case HIDPILOT_ACTION_DELAY:
            executor->deadline_ms = now_ms + action->value.delay.duration_ms;
            ++executor->action_index;
            executor->state = HIDPILOT_EXECUTOR_DELAY;
            break;
        case HIDPILOT_ACTION_MOUSE_MOVE:
            if (action->value.move.duration_ms == 0u && action->value.move.x >= -127 && action->value.move.x <= 127 &&
                action->value.move.y >= -127 && action->value.move.y <= 127) {
                if (executor->io.send_mouse(executor->io.context, 0u, (int8_t)action->value.move.x, (int8_t)action->value.move.y,
                                            action->value.move.wheel, action->value.move.pan)) {
                    ++executor->action_index;
                }
            } else {
                begin_mouse_move(executor, action, now_ms);
            }
            break;
        case HIDPILOT_ACTION_MOUSE_CLICK:
            if (executor->io.send_mouse(executor->io.context, action->value.mouse_click.buttons, 0, 0, 0, 0)) {
                executor->deadline_ms = now_ms + action->value.mouse_click.hold_ms;
                ++executor->action_index;
                executor->state = HIDPILOT_EXECUTOR_MOUSE_RELEASE;
            }
            break;
        case HIDPILOT_ACTION_KEYBOARD_CLICK:
            if (executor->io.send_keyboard(executor->io.context, action->value.keyboard_click.modifiers,
                                           action->value.keyboard_click.usage)) {
                executor->deadline_ms = now_ms + action->value.keyboard_click.hold_ms;
                ++executor->action_index;
                executor->state = HIDPILOT_EXECUTOR_KEYBOARD_RELEASE;
            }
            break;
        default:
            ++executor->error_count;
            executor->one_shot = false;
            executor->run_config = executor->active_config;
            begin_neutral_release(executor, false);
            break;
    }
}

void hidpilot_executor_tick(hidpilot_executor_t *executor, uint32_t now_ms) {
    if (!executor->mounted) {
        return;
    }
    if (executor->suspended) {
        const bool due = executor->pending_run || (executor->active_config.enabled && time_reached(now_ms, executor->next_cycle_ms));
        if (due && executor->remote_wakeup_allowed && !executor->wake_requested) {
            executor->io.request_remote_wakeup(executor->io.context);
            executor->wake_requested = true;
            executor->pending_run = true;
        }
        return;
    }

    switch (executor->state) {
        case HIDPILOT_EXECUTOR_DETACHED:
            executor->state = HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD;
            break;
        case HIDPILOT_EXECUTOR_NEUTRAL_KEYBOARD:
            if (executor->io.send_keyboard(executor->io.context, 0u, 0u)) {
                executor->state = HIDPILOT_EXECUTOR_NEUTRAL_MOUSE;
            }
            break;
        case HIDPILOT_EXECUTOR_NEUTRAL_MOUSE:
            if (executor->io.send_mouse(executor->io.context, 0u, 0, 0, 0, 0)) {
                if (executor->pending_run) {
                    start_cycle(executor, now_ms);
                } else {
                    executor->activity_active = false;
                    executor->state = HIDPILOT_EXECUTOR_WAITING;
                }
            }
            break;
        case HIDPILOT_EXECUTOR_WAITING:
            if (executor->active_config.enabled && time_reached(now_ms, executor->next_cycle_ms)) {
                executor->run_config = executor->active_config;
                start_cycle(executor, now_ms);
            }
            break;
        case HIDPILOT_EXECUTOR_ACTION:
            run_action(executor, now_ms);
            break;
        case HIDPILOT_EXECUTOR_MOUSE_MOVE:
            run_mouse_move(executor, now_ms);
            break;
        case HIDPILOT_EXECUTOR_DELAY:
            if (time_reached(now_ms, executor->deadline_ms)) {
                executor->state = HIDPILOT_EXECUTOR_ACTION;
            }
            break;
        case HIDPILOT_EXECUTOR_MOUSE_RELEASE:
            if (time_reached(now_ms, executor->deadline_ms) && executor->io.send_mouse(executor->io.context, 0u, 0, 0, 0, 0)) {
                executor->state = HIDPILOT_EXECUTOR_ACTION;
            }
            break;
        case HIDPILOT_EXECUTOR_KEYBOARD_RELEASE:
            if (time_reached(now_ms, executor->deadline_ms) && executor->io.send_keyboard(executor->io.context, 0u, 0u)) {
                executor->state = HIDPILOT_EXECUTOR_ACTION;
            }
            break;
        default:
            ++executor->error_count;
            begin_neutral_release(executor, false);
            break;
    }
}

bool hidpilot_executor_busy(const hidpilot_executor_t *executor) {
    if (executor->one_shot || executor->activity_active) {
        return true;
    }
    return executor->state == HIDPILOT_EXECUTOR_ACTION || executor->state == HIDPILOT_EXECUTOR_MOUSE_MOVE ||
           executor->state == HIDPILOT_EXECUTOR_DELAY ||
           executor->state == HIDPILOT_EXECUTOR_MOUSE_RELEASE || executor->state == HIDPILOT_EXECUTOR_KEYBOARD_RELEASE;
}

bool hidpilot_executor_activity_active(const hidpilot_executor_t *executor) {
    return executor->activity_active;
}
