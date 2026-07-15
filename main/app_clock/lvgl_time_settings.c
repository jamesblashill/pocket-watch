#include "lvgl_time_settings.h"
#include "app_theme/app_theme.h"
#include "app_rtc/rtc.h"
#include "app_net/wifi_time_sync.h"

static lv_obj_t *s_time_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_sync_btn;
static lv_obj_t *s_sync_btn_label;
static bool s_syncing;

static void time_update_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    pcf85063a_datetime_t now;
    if (rtc_get_time(&now) == ESP_OK) {
        lv_label_set_text_fmt(s_time_label, "%04d-%02d-%02d %02d:%02d:%02d",
                               now.year, now.month, now.day, now.hour, now.min, now.sec);
    }
}

/* Runs on the wifi_time_sync background task, not the LVGL task - take the
 * display lock before touching any LVGL object. */
static void sync_done_cb(wifi_time_sync_result_t result)
{
    bsp_display_lock(-1);

    s_syncing = false;
    lv_obj_remove_state(s_sync_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_sync_btn_label, "Sync Now");

    switch (result) {
    case WIFI_TIME_SYNC_OK:
        lv_label_set_text(s_status_label, "Synced");
        break;
    case WIFI_TIME_SYNC_ERR_WIFI:
        lv_label_set_text(s_status_label, "WiFi connect failed");
        break;
    case WIFI_TIME_SYNC_ERR_NTP:
    default:
        lv_label_set_text(s_status_label, "Time sync failed");
        break;
    }

    bsp_display_unlock();
}

static void sync_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_syncing) {
        return;
    }
    s_syncing = true;
    lv_obj_add_state(s_sync_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_sync_btn_label, "Syncing...");
    lv_label_set_text(s_status_label, "Connecting to WiFi...");
    wifi_time_sync_start(sync_done_cb);
}

void time_settings_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 28, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Time & Sync");

    s_time_label = lv_label_create(parent);
    lv_obj_add_style(s_time_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_22, 0);
    lv_label_set_text(s_time_label, "----------- --------");
    lv_timer_create(time_update_timer_cb, 1000, NULL);
    time_update_timer_cb(NULL);

    s_status_label = lv_label_create(parent);
    lv_obj_add_style(s_status_label, app_theme_text_style(), 0);
    lv_label_set_text(s_status_label, "Not synced this session");

    s_sync_btn = lv_button_create(parent);
    lv_obj_set_style_pad_left(s_sync_btn, 28, 0);
    lv_obj_set_style_pad_right(s_sync_btn, 28, 0);
    lv_obj_set_style_pad_top(s_sync_btn, 16, 0);
    lv_obj_set_style_pad_bottom(s_sync_btn, 16, 0);
    /* A button is not scrollable itself, so without this a slight finger
     * drift mid-tap would bubble up and swipe the tileview to another tile. */
    lv_obj_remove_flag(s_sync_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_sync_btn, sync_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_sync_btn_label = lv_label_create(s_sync_btn);
    lv_obj_set_style_text_font(s_sync_btn_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_sync_btn_label, "Sync Now");
}
