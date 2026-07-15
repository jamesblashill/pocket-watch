#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the once-a-second countdown watcher. Must be called once from
 * within the LVGL lock (it creates an lv_timer), analogous to
 * app_alarm_init(). */
void app_timer_init(void);

/* Sets the countdown length. Ignored while running or ringing so editing
 * the rollers can't stomp an in-progress countdown. */
void app_timer_set_duration(uint16_t total_sec);
uint16_t app_timer_get_duration(void);

/* Starts (or resumes) the countdown from the current remaining time. */
void app_timer_start(void);
void app_timer_pause(void);

/* Stops the countdown (if running) and/or silences the ring, then resets
 * the remaining time back to the configured duration. */
void app_timer_reset(void);

bool app_timer_is_running(void);

/* True from the moment the countdown reaches zero until the user
 * dismisses it with app_timer_stop_ringing() (or app_timer_reset()). */
bool app_timer_is_ringing(void);
void app_timer_stop_ringing(void);

/* Seconds left in the current countdown (0 once finished). */
uint16_t app_timer_get_remaining(void);

#ifdef __cplusplus
}
#endif
