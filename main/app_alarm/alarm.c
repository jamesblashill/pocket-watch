#include "alarm.h"
#include "app_rtc/rtc.h"
#include "app_clock/lvgl_screen_power_button.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG   "app_alarm"

#define ALARM_SOUND_URL   "file://sdcard/alarm/forest-birds.wav"

#define SETTINGS_NVS_NAMESPACE  "settings"
#define ENABLED_NVS_KEY         "alarm_en"
#define HOUR_NVS_KEY            "alarm_hour"
#define MIN_NVS_KEY             "alarm_min"
#define VOLUME_NVS_KEY          "volume"

#define DEFAULT_HOUR   7
#define DEFAULT_MIN    0

static bool s_enabled;
static uint8_t s_hour = DEFAULT_HOUR;
static uint8_t s_min = DEFAULT_MIN;
static uint8_t s_volume;
/* True once the current minute has been checked against the alarm time,
 * so a triggered alarm (or one silenced by the user) doesn't immediately
 * re-fire on the next 1s tick within the same minute. Re-arms itself as
 * soon as the clock moves off the alarm minute, so it fires again the
 * next day. */
static bool s_armed = true;

static uint8_t nvs_get_u8_or(nvs_handle_t h, const char *key, uint8_t fallback)
{
    uint8_t val = fallback;
    nvs_get_u8(h, key, &val);
    return val;
}

static void load_settings(void)
{
    s_volume = get_audio_volume();

    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        s_enabled = nvs_get_u8_or(h, ENABLED_NVS_KEY, 0) != 0;
        s_hour = nvs_get_u8_or(h, HOUR_NVS_KEY, DEFAULT_HOUR);
        s_min = nvs_get_u8_or(h, MIN_NVS_KEY, DEFAULT_MIN);
        s_volume = nvs_get_u8_or(h, VOLUME_NVS_KEY, s_volume);
        nvs_close(h);
    }

    if (s_hour > 23) {
        s_hour = DEFAULT_HOUR;
    }
    if (s_min > 59) {
        s_min = DEFAULT_MIN;
    }
}

static void save_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, val);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool app_alarm_get_enabled(void)
{
    return s_enabled;
}

void app_alarm_set_enabled(bool enabled)
{
    s_enabled = enabled;
    save_u8(ENABLED_NVS_KEY, (uint8_t)enabled);
}

void app_alarm_get_time(uint8_t *hour, uint8_t *min)
{
    *hour = s_hour;
    *min = s_min;
}

void app_alarm_set_time(uint8_t hour, uint8_t min)
{
    s_hour = hour;
    s_min = min;
    save_u8(HOUR_NVS_KEY, hour);
    save_u8(MIN_NVS_KEY, min);
}

uint8_t app_alarm_get_volume(void)
{
    return s_volume;
}

void app_alarm_set_volume(uint8_t volume)
{
    s_volume = volume;
    Volume_Adjustment(volume);
    save_u8(VOLUME_NVS_KEY, volume);
}

bool app_alarm_is_ringing(void)
{
    esp_asp_state_t state = Audio_Get_Current_State();
    return (state == ESP_ASP_STATE_RUNNING || state == ESP_ASP_STATE_PAUSED);
}

void app_alarm_start_ringing(void)
{
    /* Wake the screen and restore full CPU power even if it's already
     * ringing (e.g. the scheduled trigger firing right as someone hits
     * "test") - always safe, and screen_power_set_on() no-ops if already on. */
    screen_power_set_on(true);

    if (app_alarm_is_ringing()) {
        return;
    }
    ESP_LOGI(TAG, "Alarm sound starting");
    Volume_Adjustment(s_volume);
    Audio_Play_Music(ALARM_SOUND_URL);
}

void app_alarm_stop_ringing(void)
{
    if (!app_alarm_is_ringing()) {
        return;
    }
    ESP_LOGI(TAG, "Alarm sound stopping");
    Audio_Stop_Play();
}

static void alarm_check_timer_cb(void *arg)
{
    (void)arg;

    pcf85063a_datetime_t now = {0};
    if (rtc_get_time(&now) != ESP_OK) {
        return;
    }

    bool match = s_enabled && now.hour == s_hour && now.min == s_min;
    if (match) {
        if (s_armed) {
            s_armed = false;
            app_alarm_start_ringing();
        }
    } else {
        s_armed = true;
    }
}

void app_alarm_init(void)
{
    load_settings();
    Volume_Adjustment(s_volume);

    /* Deliberately an esp_timer, not an lv_timer: lv_timer callbacks only run
     * from inside lv_timer_handler(), and screen_power_set_on(false) stops
     * the LVGL worker task from calling that at all (via esp_lv_adapter_pause())
     * to save power while the screen is off. An lv_timer alarm check would
     * simply never fire while "asleep" - the one time it matters most. An
     * esp_timer runs on the esp_timer service task instead, independent of
     * the paused LVGL worker. */
    const esp_timer_create_args_t timer_args = {
        .callback = alarm_check_timer_cb,
        .name = "alarm_check",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
}
