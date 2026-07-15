#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_TIME_SYNC_OK,
    WIFI_TIME_SYNC_ERR_WIFI,   /* couldn't connect to the AP within the timeout */
    WIFI_TIME_SYNC_ERR_NTP,    /* connected, but the SNTP exchange failed */
} wifi_time_sync_result_t;

typedef void (*wifi_time_sync_cb_t)(wifi_time_sync_result_t result);

/* Connects to WiFi, syncs time via SNTP, and writes the result into the RTC
 * (see app_rtc/rtc.h), then releases WiFi again - the radio is only
 * powered for the duration of the sync, not left running afterwards.
 * Call app_rtc_init() before this.
 *
 * Runs in the background and returns immediately. A call made while a sync
 * is already in progress is ignored. cb, if non-NULL, is invoked from a
 * background task once the sync finishes (success or failure) - it must
 * not block, and should take the LVGL lock itself before touching UI. */
void wifi_time_sync_start(wifi_time_sync_cb_t cb);

#ifdef __cplusplus
}
#endif
