#include "lvgl_device_status.h"
#include "app_theme/app_theme.h"
#include "app_msg/submenu_ui/set_bat_msg.h"
#include "app_diag/power_diag.h"

#define TAG   "device_status"

static lv_obj_t *s_battery_label;
static lv_obj_t *s_state_label;

static void update_battery_ui(void)
{
    bq27220_handle_t bq27220 = bq27220_drv_get_handle();
    if (!bq27220) {
        lv_label_set_text(s_battery_label, "--%");
        lv_label_set_text(s_state_label, "Battery unavailable");
        return;
    }

    uint16_t soc = bq27220_get_state_of_charge(bq27220);
    int16_t current = bq27220_get_current(bq27220);

    lv_label_set_text_fmt(s_battery_label, "%d%%", soc);
    lv_label_set_text(s_state_label, (current < 0) ? "Discharging" : "Charging");
}

static void battery_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    update_battery_ui();
}

static void delete_log_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    power_diag_delete_log();
}

void device_status_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 12, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Device Status");

    s_battery_label = lv_label_create(parent);
    lv_obj_add_style(s_battery_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_battery_label, "--%");

    s_state_label = lv_label_create(parent);
    lv_obj_add_style(s_state_label, app_theme_text_style(), 0);
    lv_label_set_text(s_state_label, "--");

    lv_timer_create(battery_timer_cb, 1000, NULL);
    update_battery_ui();

    lv_obj_t *delete_log_btn = lv_button_create(parent);
    lv_obj_set_style_pad_left(delete_log_btn, 28, 0);
    lv_obj_set_style_pad_right(delete_log_btn, 28, 0);
    lv_obj_set_style_pad_top(delete_log_btn, 16, 0);
    lv_obj_set_style_pad_bottom(delete_log_btn, 16, 0);
    /* A button is not scrollable itself, so without this a slight finger
     * drift mid-tap would bubble up and swipe the tileview to another tile. */
    lv_obj_remove_flag(delete_log_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(delete_log_btn, delete_log_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_log_btn_label = lv_label_create(delete_log_btn);
    lv_obj_set_style_text_font(delete_log_btn_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(delete_log_btn_label, "Delete Log");
}
