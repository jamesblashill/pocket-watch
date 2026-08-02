#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start a periodic log (serial + /sdcard/power_diag.log) of battery
 *        current/SoC and held esp_pm locks, independent of LVGL (so it
 *        keeps sampling through screen-off/light-sleep periods that pause
 *        the LVGL worker). SD logging lets a run be captured fully
 *        disconnected from USB, so the current reading isn't contaminated
 *        by USB bus power or battery charging. Also prints a rolling
 *        60s-window current history to the serial console each tick, so a
 *        quick USB reconnect is enough to check in on a run without
 *        pulling the SD card. Temporary instrumentation for the
 *        BATTERY_OPTIMIZATIONS.md investigation - not meant to ship
 *        long-term. Call after msg_driver_init() (owns the bq27220
 *        handle), after bsp_sdcard_mount(), and after
 *        screen_power_button_init() (owns the CPU freq policy being
 *        reported on).
 */
void power_diag_init(void);

/**
 * @brief Delete /sdcard/power_diag.log, so the next capture run starts from
 *        an empty file instead of appending to a previous run's data.
 */
void power_diag_delete_log(void);

#ifdef __cplusplus
}
#endif
