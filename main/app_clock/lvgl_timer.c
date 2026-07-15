#include "lvgl_timer.h"
#include "app_timer/timer.h"
#include "app_theme/app_theme.h"
#include <stdio.h>

/* "00\n01\n...\n59", built once at init time. */
static char s_min_options[60 * 3 + 1];
static char s_sec_options[60 * 3 + 1];

static lv_obj_t *s_countdown_label;
static lv_obj_t *s_roller_row;
static lv_obj_t *s_min_roller;
static lv_obj_t *s_sec_roller;
static lv_obj_t *s_start_pause_label;

static void build_roller_options(char *buf, size_t buf_size, int count)
{
    size_t off = 0;
    for (int i = 0; i < count && off + 4 <= buf_size; i++) {
        off += snprintf(buf + off, buf_size - off, "%02d%s", i, (i < count - 1) ? "\n" : "");
    }
}

static void update_ui(void)
{
    bool running = app_timer_is_running();
    bool ringing = app_timer_is_ringing();
    uint16_t remaining = (running || ringing) ? app_timer_get_remaining() : app_timer_get_duration();

    lv_label_set_text_fmt(s_countdown_label, "%02d:%02d", remaining / 60, remaining % 60);

    /* Rollers set the duration, so they only make sense while the timer
     * is stopped; hide them once it's counting down or ringing. */
    if (running || ringing) {
        lv_obj_add_flag(s_roller_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_roller_row, LV_OBJ_FLAG_HIDDEN);
    }

    if (ringing) {
        lv_label_set_text(s_start_pause_label, "Stop");
    } else if (running) {
        lv_label_set_text(s_start_pause_label, "Pause");
    } else {
        lv_label_set_text(s_start_pause_label, "Start");
    }
}

static void duration_roller_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint16_t min = (uint16_t)lv_roller_get_selected(s_min_roller);
    uint16_t sec = (uint16_t)lv_roller_get_selected(s_sec_roller);
    app_timer_set_duration(min * 60 + sec);
    update_ui();
}

static void start_pause_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (app_timer_is_ringing()) {
        app_timer_stop_ringing();
    } else if (app_timer_is_running()) {
        app_timer_pause();
    } else {
        app_timer_start();
    }
    update_ui();
}

static void reset_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_timer_reset();
    update_ui();
}

static void timer_poll_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    update_ui();
}

void timer_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 20, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_label_set_text(title, "Timer");

    s_countdown_label = lv_label_create(parent);
    lv_obj_add_style(s_countdown_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_countdown_label, &lv_font_montserrat_48, 0);

    /* Duration rollers, same style as the alarm's hour/min rollers */
    build_roller_options(s_min_options, sizeof(s_min_options), 60);
    build_roller_options(s_sec_options, sizeof(s_sec_options), 60);

    uint16_t duration = app_timer_get_duration();

    s_roller_row = lv_obj_create(parent);
    lv_obj_remove_style_all(s_roller_row);
    lv_obj_set_size(s_roller_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_roller_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_roller_row, 12, 0);

    s_min_roller = lv_roller_create(s_roller_row);
    lv_roller_set_options(s_min_roller, s_min_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_min_roller, 90);
    lv_obj_set_style_text_font(s_min_roller, &lv_font_montserrat_22, 0);
    lv_roller_set_visible_row_count(s_min_roller, 2);
    lv_roller_set_selected(s_min_roller, duration / 60, LV_ANIM_OFF);
    /* This tile's swipe axis is vertical (back down to the clock), the same
     * axis the roller itself scrolls on, so a value-picking drag must not
     * bubble up and flip the tile - mirrors the volume slider fix in
     * lvgl_alarm_volume.c, just on the other axis. */
    lv_obj_remove_flag(s_min_roller, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_min_roller, duration_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *colon = lv_label_create(s_roller_row);
    lv_obj_add_style(colon, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_22, 0);
    lv_label_set_text(colon, ":");

    s_sec_roller = lv_roller_create(s_roller_row);
    lv_roller_set_options(s_sec_roller, s_sec_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_sec_roller, 90);
    lv_obj_set_style_text_font(s_sec_roller, &lv_font_montserrat_22, 0);
    lv_roller_set_visible_row_count(s_sec_roller, 2);
    lv_roller_set_selected(s_sec_roller, duration % 60, LV_ANIM_OFF);
    lv_obj_remove_flag(s_sec_roller, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_sec_roller, duration_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Button row */
    lv_obj_t *btn_row = lv_obj_create(parent);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);

    lv_obj_t *start_pause_btn = lv_button_create(btn_row);
    lv_obj_set_style_pad_left(start_pause_btn, 28, 0);
    lv_obj_set_style_pad_right(start_pause_btn, 28, 0);
    lv_obj_set_style_pad_top(start_pause_btn, 16, 0);
    lv_obj_set_style_pad_bottom(start_pause_btn, 16, 0);
    lv_obj_remove_flag(start_pause_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(start_pause_btn, start_pause_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_start_pause_label = lv_label_create(start_pause_btn);
    lv_obj_set_style_text_font(s_start_pause_label, &lv_font_montserrat_24, 0);

    lv_obj_t *reset_btn = lv_button_create(btn_row);
    lv_obj_set_style_pad_left(reset_btn, 28, 0);
    lv_obj_set_style_pad_right(reset_btn, 28, 0);
    lv_obj_set_style_pad_top(reset_btn, 16, 0);
    lv_obj_set_style_pad_bottom(reset_btn, 16, 0);
    lv_obj_remove_flag(reset_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(reset_label, "Reset");

    update_ui();
    lv_timer_create(timer_poll_timer_cb, 500, NULL);
}
