#include "lvgl_clock.h"
#include "app_rtc/rtc.h"
#include "app_theme/app_theme.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG   "clock"

#define ANALOG_FACE_SIZE   300
#define ANALOG_HOUR_LEN     65
#define ANALOG_MIN_LEN     100
#define ANALOG_SEC_LEN     125

#define SETTINGS_NVS_NAMESPACE  "settings"
#define CLOCK_FACE_NVS_KEY      "clock_face"

static clock_face_t s_mode = CLOCK_FACE_DIGITAL;

static lv_obj_t *s_digital_cont;
static lv_obj_t *s_time_label;

static lv_obj_t *s_analog_cont;
static lv_obj_t *s_analog_scale;
static lv_obj_t *s_hour_hand;
static lv_obj_t *s_min_hand;
static lv_obj_t *s_sec_hand;

static void set_mode(clock_face_t mode)
{
    s_mode = mode;
    if (mode == CLOCK_FACE_DIGITAL) {
        lv_obj_remove_flag(s_digital_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_analog_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_digital_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_analog_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

static clock_face_t load_saved_face(void)
{
    uint8_t face = CLOCK_FACE_DIGITAL;
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, CLOCK_FACE_NVS_KEY, &face);
        nvs_close(h);
    }
    return (face == CLOCK_FACE_ANALOG) ? CLOCK_FACE_ANALOG : CLOCK_FACE_DIGITAL;
}

static void save_face(clock_face_t face)
{
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, CLOCK_FACE_NVS_KEY, (uint8_t)face);
        nvs_commit(h);
        nvs_close(h);
    }
}

void clock_ui_set_face(clock_face_t face)
{
    set_mode(face);
    save_face(face);
}

clock_face_t clock_ui_get_face(void)
{
    return s_mode;
}

static void clock_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    pcf85063a_datetime_t now = {0};
    if (rtc_get_time(&now) != ESP_OK) {
        return;
    }

    lv_label_set_text_fmt(s_time_label, "%02d:%02d", now.hour, now.min);

    /* Scale range is 0-3600, i.e. tenths-of-a-degree around the full circle,
     * with 0 pointing to 12 o'clock (rotation set below). Each hand's value is
     * its own elapsed time folded into that same range so all three needles
     * can share one lv_scale. */
    int32_t hour_val = ((int32_t)(now.hour % 12) * 3600 + now.min * 60 + now.sec) / 12;
    int32_t min_val  = now.min * 60 + now.sec;
    int32_t sec_val  = now.sec * 60;

    lv_scale_set_line_needle_value(s_analog_scale, s_hour_hand, ANALOG_HOUR_LEN, hour_val);
    lv_scale_set_line_needle_value(s_analog_scale, s_min_hand, ANALOG_MIN_LEN, min_val);
    lv_scale_set_line_needle_value(s_analog_scale, s_sec_hand, ANALOG_SEC_LEN, sec_val);
}

static void build_digital(lv_obj_t *parent)
{
    s_digital_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_digital_cont);
    lv_obj_set_size(s_digital_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_digital_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_digital_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(s_digital_cont, app_theme_bg_style(), 0);

    s_time_label = lv_label_create(s_digital_cont);
    lv_obj_add_style(s_time_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_time_label, "00:00");

    /* lv_font_montserrat_48 is the largest bitmap font this build compiles in;
     * scale it up further so the digits land at roughly 3.6x the previous
     * (montserrat_26) size, now that there's no seconds row competing for
     * vertical space. The pivot must be set to the label's own center
     * (computed after layout) or the scaled text drifts down-and-right. */
    lv_obj_update_layout(s_time_label);
    lv_obj_set_style_transform_pivot_x(s_time_label, lv_obj_get_width(s_time_label) / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_time_label, lv_obj_get_height(s_time_label) / 2, 0);
    lv_obj_set_style_transform_scale_x(s_time_label, 460, 0);
    lv_obj_set_style_transform_scale_y(s_time_label, 460, 0);
}

static void build_analog(lv_obj_t *parent)
{
    s_analog_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_analog_cont);
    lv_obj_set_size(s_analog_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(s_analog_cont, app_theme_bg_style(), 0);

    s_analog_scale = lv_scale_create(s_analog_cont);
    lv_obj_set_size(s_analog_scale, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
    lv_obj_center(s_analog_scale);
    lv_scale_set_mode(s_analog_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_label_show(s_analog_scale, false);
    lv_scale_set_total_tick_count(s_analog_scale, 60);
    lv_scale_set_major_tick_every(s_analog_scale, 5);
    lv_scale_set_angle_range(s_analog_scale, 360);
    lv_scale_set_rotation(s_analog_scale, 270);
    lv_scale_set_range(s_analog_scale, 0, 3600);

    /* Minimalist pocket-watch dial: cream face, thin dark bezel, plain hour ticks */
    lv_obj_set_style_bg_color(s_analog_scale, lv_color_hex(0xFAF6EC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_analog_scale, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_analog_scale, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_analog_scale, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_analog_scale, lv_color_hex(0x2B2B2B), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_analog_scale, 3, LV_PART_MAIN);

    lv_obj_set_style_line_color(s_analog_scale, lv_color_hex(0x2B2B2B), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_analog_scale, 2, LV_PART_INDICATOR);
    lv_obj_set_style_length(s_analog_scale, 14, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(s_analog_scale, lv_color_hex(0xB8B0A0), LV_PART_ITEMS);
    lv_obj_set_style_line_width(s_analog_scale, 1, LV_PART_ITEMS);
    lv_obj_set_style_length(s_analog_scale, 6, LV_PART_ITEMS);

    /* Hands, thinnest to thickest so the second hand reads as a fine sweep */
    s_hour_hand = lv_line_create(s_analog_scale);
    lv_obj_set_style_line_width(s_hour_hand, 6, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_hour_hand, true, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_hour_hand, lv_color_hex(0x1A1A1A), LV_PART_MAIN);

    s_min_hand = lv_line_create(s_analog_scale);
    lv_obj_set_style_line_width(s_min_hand, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_min_hand, true, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_min_hand, lv_color_hex(0x1A1A1A), LV_PART_MAIN);

    s_sec_hand = lv_line_create(s_analog_scale);
    lv_obj_set_style_line_width(s_sec_hand, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_sec_hand, true, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_sec_hand, lv_color_hex(0xA33B2E), LV_PART_MAIN);

    lv_obj_t *pivot = lv_obj_create(s_analog_scale);
    lv_obj_remove_style_all(pivot);
    lv_obj_set_size(pivot, 12, 12);
    lv_obj_set_style_bg_color(pivot, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(pivot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(pivot);
}

void clock_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);

    build_digital(parent);
    build_analog(parent);
    set_mode(load_saved_face());

    lv_timer_create(clock_timer_cb, 1000, NULL);
    clock_timer_cb(NULL);
}
