#include "power_diag.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "bsp/esp-bsp.h"

#include "app_msg/submenu_ui/set_bat_msg.h"
#include "app_clock/lvgl_screen_power_button.h"

#define TAG                "power_diag"
#define SAMPLE_PERIOD_US   (5 * 1000 * 1000)

/* Written to the SD card (already mounted at boot) rather than only to the
 * serial console, so a run can be captured with USB fully disconnected -
 * the point of this exercise is a battery-only current reading, and USB
 * presence can itself power the board or charge the battery, which would
 * contaminate the very number we're trying to measure. Pull the card after
 * the run to read it. */
#define LOG_PATH           (BSP_SD_MOUNT_POINT "/power_diag.log")

static esp_timer_handle_t s_timer;

static void diag_timer_cb(void *arg)
{
    (void)arg;

    bq27220_handle_t bq27220 = bq27220_drv_get_handle();
    bool have_battery = (bq27220 != NULL);
    uint16_t soc = have_battery ? bq27220_get_state_of_charge(bq27220) : 0;
    int16_t current_ma = have_battery ? bq27220_get_current(bq27220) : 0;

    /* Same timestamp source ESP_LOG itself stamps every other console line
     * with (CONFIG_LOG_TIMESTAMP_SOURCE_RTOS, ms since boot), so this line
     * lines up with the rest of the serial log when correlating events.
     * Built once and written verbatim to both destinations so a captured
     * SD card log can be diffed/greped against a serial capture of the
     * same run. */
    char line[128];
    snprintf(line, sizeof(line), "t=%ums screen=%s soc=%u%% current=%dmA",
             (unsigned)esp_log_timestamp(),
             screen_power_is_on() ? "on" : "off", soc, current_ma);

    printf("%s\n", line);

    FILE *f = fopen(LOG_PATH, "a");
    if (!f) {
        ESP_LOGW(TAG, "could not open %s for append", LOG_PATH);
        return;
    }

    fprintf(f, "%s\n", line);
    fclose(f);
    /* esp_pm_dump_locks() dropped from the periodic loop: it already did
     * its job confirming the I2S-lock hypothesis earlier, and calling it
     * every 5s - writing its output to a FILE* backed by (slow, blocking)
     * SD card I/O - is a plausible deadlock source in its own right. It
     * likely briefly holds the same internal esp_pm lock-list state that
     * esp_pm_configure()/light-sleep entry needs, and blocking on SD I/O
     * while holding that would contend with the real PM transition we're
     * trying to observe. Call it standalone (stdout only, not from this
     * timer) if the lock breakdown is needed again. */
}

void power_diag_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = diag_timer_cb,
        .name = "power_diag",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, SAMPLE_PERIOD_US));
}

void power_diag_delete_log(void)
{
    if (remove(LOG_PATH) != 0) {
        ESP_LOGW(TAG, "could not delete %s", LOG_PATH);
        return;
    }
    ESP_LOGI(TAG, "deleted %s", LOG_PATH);
}
