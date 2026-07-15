#include "lvgl_alarm_volume.h"
#include "app_alarm/alarm.h"
#include "app_theme/app_theme.h"
#include "bsp_board_extra.h"

static lv_obj_t *s_volume_label;
static lv_obj_t *s_test_btn_label;

static void volume_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t vol = lv_slider_get_value(slider);
    app_alarm_set_volume((uint8_t)vol);
    lv_label_set_text_fmt(s_volume_label, "Volume: %d%%", (int)vol);
}

static void update_test_btn_label(void)
{
    lv_label_set_text(s_test_btn_label, app_alarm_is_ringing() ? "Stop Alarm" : "Play Alarm");
}

static void test_alarm_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (app_alarm_is_ringing()) {
        app_alarm_stop_ringing();
    } else {
        app_alarm_start_ringing();
    }
    update_test_btn_label();
}

static void alarm_state_poll_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    update_test_btn_label();
}

void alarm_volume_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 28, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Alarm Volume");

    uint8_t vol = app_alarm_get_volume();

    s_volume_label = lv_label_create(parent);
    lv_obj_add_style(s_volume_label, app_theme_text_style(), 0);
    lv_label_set_text_fmt(s_volume_label, "Volume: %d%%", (int)vol);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 260, 24);
    lv_slider_set_range(slider, 0, Volume_MAX);
    lv_slider_set_value(slider, vol, LV_ANIM_OFF);
    /* Sliders already stop horizontal drags from bubbling to the tileview
     * (LVGL removes SCROLL_CHAIN_HOR for horizontal sliders); clearing it
     * explicitly here documents that a slide must never flip the tile. */
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *test_btn = lv_button_create(parent);
    lv_obj_set_style_pad_left(test_btn, 28, 0);
    lv_obj_set_style_pad_right(test_btn, 28, 0);
    lv_obj_set_style_pad_top(test_btn, 16, 0);
    lv_obj_set_style_pad_bottom(test_btn, 16, 0);
    /* A button is not scrollable itself, so without this a slight finger
     * drift mid-tap would bubble up and swipe the tileview to another tile. */
    lv_obj_remove_flag(test_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(test_btn, test_alarm_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_test_btn_label = lv_label_create(test_btn);
    lv_obj_set_style_text_font(s_test_btn_label, &lv_font_montserrat_24, 0);
    update_test_btn_label();

    lv_timer_create(alarm_state_poll_timer_cb, 500, NULL);
}
