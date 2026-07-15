#include "app_theme.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG   "app_theme"

#define SETTINGS_NVS_NAMESPACE  "settings"
#define THEME_NVS_KEY           "theme"

#define LIGHT_BG_COLOR    0xFAFAFA
#define LIGHT_TEXT_COLOR  0x1A1A1A
#define DARK_BG_COLOR     0x1A1A1A
#define DARK_TEXT_COLOR   0xF2F2F2

static app_theme_mode_t s_mode = APP_THEME_LIGHT;
static lv_style_t s_style_bg;
static lv_style_t s_style_text;

static app_theme_mode_t load_saved_mode(void)
{
    uint8_t mode = APP_THEME_LIGHT;
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, THEME_NVS_KEY, &mode);
        nvs_close(h);
    }
    return (mode == APP_THEME_DARK) ? APP_THEME_DARK : APP_THEME_LIGHT;
}

static void save_mode(app_theme_mode_t mode)
{
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, THEME_NVS_KEY, (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void apply_mode_colors(app_theme_mode_t mode)
{
    bool dark = (mode == APP_THEME_DARK);
    lv_style_set_bg_color(&s_style_bg, lv_color_hex(dark ? DARK_BG_COLOR : LIGHT_BG_COLOR));
    lv_style_set_text_color(&s_style_text, lv_color_hex(dark ? DARK_TEXT_COLOR : LIGHT_TEXT_COLOR));
}

void app_theme_init(void)
{
    s_mode = load_saved_mode();

    lv_style_init(&s_style_bg);
    lv_style_set_bg_opa(&s_style_bg, LV_OPA_COVER);

    lv_style_init(&s_style_text);

    apply_mode_colors(s_mode);
}

app_theme_mode_t app_theme_get_mode(void)
{
    return s_mode;
}

void app_theme_set_mode(app_theme_mode_t mode)
{
    if (mode == s_mode) {
        return;
    }
    s_mode = mode;
    apply_mode_colors(mode);

    /* The style objects are mutated in place (not replaced), so this walks
     * every screen and refreshes any object already wearing them. */
    lv_obj_report_style_change(&s_style_bg);
    lv_obj_report_style_change(&s_style_text);

    save_mode(mode);
}

lv_style_t *app_theme_bg_style(void)
{
    return &s_style_bg;
}

lv_style_t *app_theme_text_style(void)
{
    return &s_style_text;
}
