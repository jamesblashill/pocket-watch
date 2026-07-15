#include "lvgl_screen_brightness.h"
#include "app_theme/app_theme.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG   "screen_brightness"

#define SETTINGS_NVS_NAMESPACE  "settings"
#define BRIGHTNESS_NVS_KEY      "brightness"

static lv_obj_t *s_brightness_label;

static uint8_t load_saved_brightness(uint8_t fallback)
{
    uint8_t brightness = fallback;
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, BRIGHTNESS_NVS_KEY, &brightness);
        nvs_close(h);
    }
    return brightness;
}

static void save_brightness(uint8_t brightness)
{
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BRIGHTNESS_NVS_KEY, brightness);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t brightness = lv_slider_get_value(slider);

    bsp_display_brightness_set((int)brightness);
    lv_label_set_text_fmt(s_brightness_label, "Brightness: %d%%", (int)brightness);
}

static void brightness_slider_released_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    save_brightness((uint8_t)lv_slider_get_value(slider));
}

void screen_brightness_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 28, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Screen Brightness");

    uint8_t cur_brightness = load_saved_brightness(bsp_display_brightness_get());
    bsp_display_brightness_set(cur_brightness);

    s_brightness_label = lv_label_create(parent);
    lv_obj_add_style(s_brightness_label, app_theme_text_style(), 0);
    lv_label_set_text_fmt(s_brightness_label, "Brightness: %d%%", (int)cur_brightness);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 260, 24);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, cur_brightness, LV_ANIM_OFF);
    /* Sliders already stop horizontal drags from bubbling to the tileview
     * (LVGL removes SCROLL_CHAIN_HOR for horizontal sliders); clearing it
     * explicitly here documents that a slide must never flip the tile. */
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_slider_released_event_cb, LV_EVENT_RELEASED, NULL);
}
