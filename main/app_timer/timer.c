#include "timer.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_log.h"

#define TAG   "app_timer"

#define TIMER_SOUND_URL   "file://sdcard/timer/beep.wav"

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

static void timer_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

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
        Audio_Play_Music(TIMER_SOUND_URL);
    }
}

void app_timer_init(void)
{
    lv_timer_create(timer_tick_cb, 1000, NULL);
}
