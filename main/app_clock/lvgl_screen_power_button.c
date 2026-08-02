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
    /* Light sleep is back on for another trial run: with it enabled the
     * device was freezing solid (no panic, no watchdog trigger, not even
     * the independent power_diag esp_timer kept ticking) right as it was
     * about to actually enter automatic light sleep for the first time -
     * confirmed via light_sleep_enable=false bisection to be the sleep
     * transition itself, not anything at the task level (CPU-domain
     * power-down and PSRAM XIP were already ruled out earlier). Octal
     * PSRAM at 80MHz is the tightest-margin PSRAM config on the S3, and
     * that margin gets spent exactly during the cache-suspend/resume every
     * sleep entry/exit does - sdkconfig(.defaults) now runs PSRAM at 40MHz
     * to buy headroom there. If it hangs again at 40MHz, that wasn't it and
     * this needs to go back to false. */
    .light_sleep_enable = true,
};

static bool s_screen_on = true;
static int s_saved_brightness = 100;
static lv_display_t *s_disp = NULL;
/* bsp_sdcard_mount() runs in app_main() shortly after this module is
 * initialized, so by the time a long press is possible the card is mounted. */
static bool s_sdcard_mounted = true;

void screen_power_set_display(lv_display_t *disp)
{
    s_disp = disp;
}

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
        /* The panel/panel_io/QSPI bus were fully torn down on the way to
         * sleep (see the off branch below) - ESP32-S3 has no hardware
         * support for preserving a DMA-driven peripheral's state across
         * automatic light sleep, so rather than trust it survived, rebuild
         * it fresh every wake, the same way it was built at boot. Must
         * happen before resume(): rebinding touches the display's panel
         * pointer, which needs to happen while the worker is still paused. */
        esp_err_t panel_err = ESP_ERR_INVALID_STATE;
        if (s_disp) {
            esp_lcd_panel_handle_t new_panel = NULL;
            esp_lcd_panel_io_handle_t new_io = NULL;
            panel_err = bsp_display_new_panel(&new_panel, &new_io);
            if (panel_err == ESP_OK) {
                panel_err = esp_lv_adapter_rebind_lcd_panel_internal(s_disp, new_panel, new_io);
            }
        }
        esp_err_t resume_err = esp_lv_adapter_resume();
        /* DISPON needs a moment to settle before the next flush reaches the
         * panel, or the first frame back can show a brief glitch. */
        esp_err_t dispon_err = esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), true);
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "screen on: panel=%s resume=%s dispon=%s restoring brightness=%d",
                 esp_err_to_name(panel_err), esp_err_to_name(resume_err), esp_err_to_name(dispon_err),
                 s_saved_brightness);
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
        /* pause() only guarantees the worker won't START a new flush - the
         * one its last lv_timer_handler() call may have just kicked off can
         * still be in flight (DMA-driven, completed later via an IO
         * callback) when pause() returns. Letting light sleep clock-gate
         * the QSPI/GDMA peripheral out from under that in-flight transfer
         * is a plausible reason the panel has come back blank after a real
         * sleep cycle - wait here for the bus to genuinely go idle before
         * touching backlight/panel/PM state at all. */
        err = esp_lv_adapter_wait_flush_idle(200);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_lv_adapter_wait_flush_idle failed: %s - leaving screen on, will retry", esp_err_to_name(err));
            esp_lv_adapter_resume();
            return;
        }
        s_saved_brightness = bsp_display_brightness_get();
        ESP_LOGI(TAG, "screen off: saving brightness=%d", s_saved_brightness);
        bsp_display_backlight_off();
        /* ST77916 driver has no SLPIN/SLPOUT support, so DISPOFF is the best
         * available low-power step short of patching the panel driver (see
         * BATTERY_OPTIMIZATIONS.md #3). */
        esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), false);
        /* Fully tear the panel/panel_io/QSPI bus down rather than leave them
         * attached through the sleep window. ESP32-S3 has no peripheral
         * sleep-retention hardware, so a DMA-driven bus like this one isn't
         * guaranteed to still work after a real automatic-light-sleep cycle
         * - confirmed on-device: the panel came back blank and permanently
         * wedged the LVGL render pipeline (every later esp_lv_adapter_pause()
         * timed out) after sleeping with the bus left "live". Detach first
         * so LVGL switches to dummy-draw before the panel handle goes away
         * under it; rebuilt fresh in the `on` branch above on next wake. */
        if (s_disp) {
            esp_err_t detach_err = esp_lv_adapter_detach_panel(s_disp);
            if (detach_err != ESP_OK) {
                ESP_LOGW(TAG, "esp_lv_adapter_detach_panel failed: %s", esp_err_to_name(detach_err));
            }
        }
        esp_err_t free_err = bsp_display_free_panel();
        if (free_err != ESP_OK) {
            ESP_LOGW(TAG, "bsp_display_free_panel failed: %s", esp_err_to_name(free_err));
        }
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
