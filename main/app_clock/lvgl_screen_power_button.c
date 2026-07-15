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
    .light_sleep_enable = true,
};

static bool s_screen_on = true;
static int s_saved_brightness = 100;

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
        esp_lv_adapter_resume();
        /* DISPON needs a moment to settle before the next flush reaches the
         * panel, or the first frame back can show a brief glitch. */
        esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), true);
        vTaskDelay(pdMS_TO_TICKS(20));
        bsp_display_brightness_set(s_saved_brightness);
        lv_display_trigger_activity(NULL);
    } else {
        s_saved_brightness = bsp_display_brightness_get();
        bsp_display_backlight_off();
        /* ST77916 driver has no SLPIN/SLPOUT support, so DISPOFF is the best
         * available low-power step short of patching the panel driver (see
         * BATTERY_OPTIMIZATIONS.md #3). */
        esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), false);
        /* Stops the LVGL worker task from doing any further render/compositing
         * work while nothing is visible. Safe to call from the worker task's
         * own context (e.g. the idle_check_timer_cb below) - the adapter
         * detects that case and acks immediately instead of deadlocking. */
        esp_err_t err = esp_lv_adapter_pause(100);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_lv_adapter_pause failed: %s", esp_err_to_name(err));
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
}

void screen_power_idle_monitor_init(void)
{
    lv_timer_create(idle_check_timer_cb, 1000, NULL);
}
