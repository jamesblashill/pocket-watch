#include "timer.h"
#include "app_clock/lvgl_screen_power_button.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG   "app_timer"

/* Reuse the alarm's sound file rather than a dedicated timer/ one on the SD
 * card - keeps this working without requiring a separate asset to be
 * provisioned. */
#define TIMER_SOUND_URL   "file://sdcard/alarm/forest-birds.wav"

#define DEFAULT_DURATION_SEC   (5 * 60)

static uint16_t s_duration_sec = DEFAULT_DURATION_SEC;
static uint16_t s_remaining_sec = DEFAULT_DURATION_SEC;
static bool s_running;
static bool s_ringing;

void app_timer_set_duration(uint16_t total_sec)
{
    s_duration_sec = total_sec;
    if (!s_running && !s_ringing) {
        s_remaining_sec = total_sec;
    }
}

uint16_t app_timer_get_duration(void)
{
    return s_duration_sec;
}

void app_timer_start(void)
{
    if (s_ringing) {
        app_timer_stop_ringing();
    }
    if (s_remaining_sec == 0) {
        s_remaining_sec = s_duration_sec;
    }
    s_running = true;
}

void app_timer_pause(void)
{
    s_running = false;
}

void app_timer_reset(void)
{
    s_running = false;
    app_timer_stop_ringing();
    s_remaining_sec = s_duration_sec;
}

bool app_timer_is_running(void)
{
    return s_running;
}

bool app_timer_is_ringing(void)
{
    return s_ringing;
}

void app_timer_stop_ringing(void)
{
    if (!s_ringing) {
        return;
    }
    s_ringing = false;
    Audio_Stop_Play();
}

uint16_t app_timer_get_remaining(void)
{
    return s_remaining_sec;
}

static void timer_tick_cb(void *arg)
{
    (void)arg;

    if (s_ringing) {
        /* beep.wav is a short one-shot clip; re-firing it whenever playback
         * has finished turns it into a repeating beep until the user
         * dismisses the timer. */
        esp_asp_state_t state = Audio_Get_Current_State();
        if (state != ESP_ASP_STATE_RUNNING && state != ESP_ASP_STATE_PAUSED) {
            Audio_Play_Music(TIMER_SOUND_URL);
        }
        return;
    }

    if (!s_running) {
        return;
    }

    if (s_remaining_sec > 0) {
        s_remaining_sec--;
    }

    if (s_remaining_sec == 0) {
        s_running = false;
        s_ringing = true;
        ESP_LOGI(TAG, "Timer finished, ringing");
        /* Wake the screen the same way the alarm does, in case it finishes
         * while the screen is off. */
        screen_power_set_on(true);
        Audio_Play_Music(TIMER_SOUND_URL);
    }
}

void app_timer_init(void)
{
    /* Deliberately an esp_timer, not an lv_timer: lv_timer callbacks only run
     * from inside lv_timer_handler(), and screen_power_set_on(false) stops
     * the LVGL worker task from calling that at all (via esp_lv_adapter_pause())
     * to save power while the screen is off. An lv_timer countdown would
     * simply stop ticking while "asleep", so the timer would appear paused
     * whenever the screen turns off. An esp_timer runs on the esp_timer
     * service task instead, independent of the paused LVGL worker. See
     * app_alarm_init() for the same reasoning. */
    const esp_timer_create_args_t timer_args = {
        .callback = timer_tick_cb,
        .name = "app_timer_tick",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
}
