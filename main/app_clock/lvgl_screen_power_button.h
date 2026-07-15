#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wire up the board's BOOT button (GPIO0) to toggle the LCD backlight
 *        on/off, so the screen can be turned off and back on without
 *        touching the capacitive touchscreen. If the alarm is currently
 *        ringing, a press also silences it (in addition to toggling the
 *        screen). Also starts a 15s idle-timeout timer that blanks the
 *        screen automatically (same effect as the button) after that long
 *        without a touch, and sets the initial (full-power) CPU frequency
 *        policy - see screen_power_set_on().
 */
void screen_power_button_init(void);

/**
 * @brief Turn the backlight on/off and switch the CPU frequency policy to
 *        match: full 240MHz with light sleep disabled while the screen is
 *        on, throttled down with light sleep enabled while it's off (just
 *        enough to service the alarm-check timer and react to the power
 *        button or an alarm). Callable from anywhere that needs to force
 *        the screen on, e.g. the alarm module when it starts ringing.
 */
void screen_power_set_on(bool on);
bool screen_power_is_on(void);

#ifdef __cplusplus
}
#endif
