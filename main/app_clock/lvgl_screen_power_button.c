#include "lvgl_screen_power_button.h"
#include "app_alarm/alarm.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG   "screen_power_btn"

/* Auto-blank after this long without a touch, same as pressing the power
 * button by hand. */
#define IDLE_TIMEOUT_MS   15000

/* Full power while the screen is on, so touch and LVGL rendering stay
 * snappy. Throttled while the screen is off: the LVGL worker is paused (see
 * esp_lv_adapter_pause() below) so only the 1s alarm-check timer needs to
 * run, letting the CPU idle at a low frequency and light-sleep between
 * wakes, waking for the power button or a fired alarm. The esp_lvgl_adapter
 * fork in components/esp_lvgl_adapter drives LVGL's tick from
 * esp_timer_get_time() on demand instead of a periodic esp_timer, so there's
 * no periodic tick source left to cap the sleep window - light sleep can now
 * run for as long as the next real wake source allows. 40MHz is this
 * board's XTAL frequency - the lowest frequency the chip can run at (rather
 * than sleep at) while light sleep is enabled. */
static const esp_pm_config_t s_pm_full_power = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false,
};
static const esp_pm_config_t s_pm_screen_off = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 40,
    /* Light sleep is back off again, temporarily, to isolate a second hang:
     * with it enabled the device now freezes solid (no panic, no watchdog
     * trigger, not even the independent power_diag esp_timer keeps ticking)
     * right as it's about to actually enter automatic light sleep for the
     * first time - consistent with something below the OS (cache/MSPI/PSRAM
     * suspend-resume around the sleep transition itself) rather than
     * anything at the task level. Bisection previously showed DFS alone
     * (240->40MHz, no sleep) is safe, so that's what's left here while the
     * light-sleep-entry hang gets tracked down. Flip back to true only once
     * that's resolved. */
    .light_sleep_enable = false,
};

static bool s_screen_on = true;
static int s_saved_brightness = 100;
/* bsp_sdcard_mount() runs in app_main() shortly after this module is
 * initialized, so by the time a long press is possible the card is mounted. */
static bool s_sdcard_mounted = true;

static void apply_power_state(bool on)
{
    esp_err_t err = esp_pm_configure(on ? &s_pm_full_power : &s_pm_screen_off);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }
}

void screen_power_set_on(bool on)
{
    if (on == s_screen_on) {
        return;
    }

    if (on) {
        gpio_wakeup_disable(BSP_LCD_TOUCH_INT);
        esp_err_t resume_err = esp_lv_adapter_resume();
        /* DISPON needs a moment to settle before the next flush reaches the
         * panel, or the first frame back can show a brief glitch. */
        esp_err_t dispon_err = esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), true);
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "screen on: resume=%s dispon=%s restoring brightness=%d",
                 esp_err_to_name(resume_err), esp_err_to_name(dispon_err), s_saved_brightness);
        bsp_display_brightness_set(s_saved_brightness);
        lv_display_trigger_activity(NULL);
    } else {
        /* Stops the LVGL worker task from doing any further render/compositing
         * work while nothing is visible. Safe to call from the worker task's
         * own context (e.g. the idle_check_timer_cb below) - the adapter
         * detects that case and acks immediately instead of deadlocking.
         * Tried first, before touching the backlight/panel/PM state: if the
         * worker doesn't actually confirm it stopped (e.g. because it's
         * wedged on something unrelated, like a stuck SD card operation),
         * pressing ahead into a light-sleep transition on top of that has
         * produced unrecoverable hangs - bail out and leave the screen on
         * instead, so the idle-check timer just retries next second. */
        esp_err_t err = esp_lv_adapter_pause(100);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_lv_adapter_pause failed: %s - leaving screen on, will retry", esp_err_to_name(err));
            return;
        }
        s_saved_brightness = bsp_display_brightness_get();
        ESP_LOGI(TAG, "screen off: saving brightness=%d", s_saved_brightness);
        bsp_display_backlight_off();
        /* ST77916 driver has no SLPIN/SLPOUT support, so DISPOFF is the best
         * available low-power step short of patching the panel driver (see
         * BATTERY_OPTIMIZATIONS.md #3). */
        esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), false);
        /* The touch driver never registers a GPIO ISR on this pin (LVGL polls
         * get_xy() instead - see esp_lcd_touch_register_interrupt_callback,
         * never called from bsp_touch_new), so it's safe to repurpose its
         * interrupt config for wake here; gpio_wakeup_enable() overwrites the
         * pin's interrupt type as a side effect. The power button doesn't
         * need the same treatment - iot_button's own enable_power_save
         * handling (screen_power_button_init below) arms/disarms its GPIO
         * wakeup automatically. Without this, light sleep (now unbounded per
         * the tick-timer fix above) would have no touch-driven wake source. */
        ESP_ERROR_CHECK(gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL));
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    }
    apply_power_state(on);
    s_screen_on = on;
}

bool screen_power_is_on(void)
{
    return s_screen_on;
}

static void idle_check_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    if (!s_screen_on || app_alarm_is_ringing()) {
        return;
    }
    if (lv_display_get_inactive_time(NULL) >= IDLE_TIMEOUT_MS) {
        screen_power_set_on(false);
    }
}

static void power_button_click_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;

    if (app_alarm_is_ringing()) {
        app_alarm_stop_ringing();
    }

    screen_power_set_on(!s_screen_on);
}

/* Hold the power button to unmount the SD card before pulling it, so any
 * buffered writes get flushed instead of risking FAT corruption. iot_button
 * treats long press and single click as mutually exclusive on release, so
 * this never fires alongside power_button_click_cb above. */
static void power_button_long_press_cb(void *arg, void *data)
{
    (void)arg;
    (void)data;

    bsp_display_lock(-1);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "SD Card");
    if (!s_sdcard_mounted) {
        lv_msgbox_add_text(mbox, "Already unmounted - safe to remove.");
    } else if (bsp_sdcard_unmount() == ESP_OK) {
        s_sdcard_mounted = false;
        lv_msgbox_add_text(mbox, "Unmounted - safe to remove.");
    } else {
        lv_msgbox_add_text(mbox, "Unmount failed - do not remove.");
    }
    lv_msgbox_add_close_button(mbox);
    bsp_display_unlock();
}

void screen_power_button_init(void)
{
    apply_power_state(true);

    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BSP_BUTTONS_IO_0,
        .active_level = 0,
        .enable_power_save = true,
    };

    button_handle_t btn = NULL;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power button on GPIO%d: %d", (int)btn_gpio_cfg.gpio_num, ret);
        return;
    }

    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, power_button_click_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, power_button_long_press_cb, NULL);
}

void screen_power_idle_monitor_init(void)
{
    lv_timer_create(idle_check_timer_cb, 1000, NULL);
}
