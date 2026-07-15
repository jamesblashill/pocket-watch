#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads saved alarm/volume settings from NVS and starts the once-a-second
 * clock watcher (an esp_timer, independent of the LVGL worker so it keeps
 * running even while the screen - and LVGL - is paused/asleep) that fires
 * the alarm sound when the RTC time matches. Must be called once, after
 * app_rtc_init(). */
void app_alarm_init(void);

bool app_alarm_get_enabled(void);
void app_alarm_set_enabled(bool enabled);

/* hour: 0-23, min: 0-59 */
void app_alarm_get_time(uint8_t *hour, uint8_t *min);
void app_alarm_set_time(uint8_t hour, uint8_t min);

uint8_t app_alarm_get_volume(void);
void app_alarm_set_volume(uint8_t volume);

/* Starts the alarm sound immediately (used both for the "test" button and
 * by the scheduled trigger), and silences it. Both are safe to call
 * regardless of current state. */
void app_alarm_start_ringing(void);
void app_alarm_stop_ringing(void);
bool app_alarm_is_ringing(void);

#ifdef __cplusplus
}
#endif
