#include "lvgl_alarm_settings.h"
#include "app_alarm/alarm.h"
#include "app_theme/app_theme.h"
#include <stdio.h>

/* "00\n01\n...\n23" / "00\n...\n59", built once at init time. */
static char s_hour_options[24 * 3 + 1];
static char s_min_options[60 * 3 + 1];

static lv_obj_t *s_hour_roller;
static lv_obj_t *s_min_roller;

static void build_roller_options(char *buf, size_t buf_size, int count)
{
    size_t off = 0;
    for (int i = 0; i < count && off + 4 <= buf_size; i++) {
        off += snprintf(buf + off, buf_size - off, "%02d%s", i, (i < count - 1) ? "\n" : "");
    }
}

static void enabled_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_alarm_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void time_roller_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint8_t hour = (uint8_t)lv_roller_get_selected(s_hour_roller);
    uint8_t min = (uint8_t)lv_roller_get_selected(s_min_roller);
    app_alarm_set_time(hour, min);
}

void alarm_settings_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 36, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Alarm");

    /* Enable row */
    lv_obj_t *enable_row = lv_obj_create(parent);
    lv_obj_remove_style_all(enable_row);
    lv_obj_set_size(enable_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enable_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(enable_row, 20, 0);

    lv_obj_t *enable_label = lv_label_create(enable_row);
    lv_obj_add_style(enable_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(enable_label, &lv_font_montserrat_22, 0);
    lv_label_set_text(enable_label, "Enabled");

    lv_obj_t *sw = lv_switch_create(enable_row);
    lv_obj_set_size(sw, 100, 50);
    /* Without this, a slight finger drift mid-tap bubbles up and swipes
     * the tileview to another tile instead of toggling the switch. */
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLL_CHAIN);
    if (app_alarm_get_enabled()) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, enabled_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Time row - the tile's main event, so it gets the most space */
    uint8_t alarm_hour, alarm_min;
    app_alarm_get_time(&alarm_hour, &alarm_min);

    build_roller_options(s_hour_options, sizeof(s_hour_options), 24);
    build_roller_options(s_min_options, sizeof(s_min_options), 60);

    lv_obj_t *time_row = lv_obj_create(parent);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_row, 12, 0);

    s_hour_roller = lv_roller_create(time_row);
    lv_roller_set_options(s_hour_roller, s_hour_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_hour_roller, 110);
    lv_obj_set_style_text_font(s_hour_roller, &lv_font_montserrat_26, 0);
    lv_roller_set_visible_row_count(s_hour_roller, 3);
    lv_roller_set_selected(s_hour_roller, alarm_hour, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_hour_roller, time_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *colon = lv_label_create(time_row);
    lv_obj_add_style(colon, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_26, 0);
    lv_label_set_text(colon, ":");

    s_min_roller = lv_roller_create(time_row);
    lv_roller_set_options(s_min_roller, s_min_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_width(s_min_roller, 110);
    lv_obj_set_style_text_font(s_min_roller, &lv_font_montserrat_26, 0);
    lv_roller_set_visible_row_count(s_min_roller, 3);
    lv_roller_set_selected(s_min_roller, alarm_min, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_min_roller, time_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
