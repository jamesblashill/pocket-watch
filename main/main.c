#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "app_alarm/alarm.h"
#include "app_timer/timer.h"
#include "app_msg/sys_msg_ui.h"
#include "app_clock/lvgl_clock.h"
#include "app_clock/lvgl_timer.h"
#include "app_clock/lvgl_device_status.h"
#include "app_clock/lvgl_alarm_settings.h"
#include "app_clock/lvgl_alarm_volume.h"
#include "app_clock/lvgl_screen_brightness.h"
#include "app_clock/lvgl_screen_power_button.h"
#include "app_clock/lvgl_theme_settings.h"
#include "app_diag/power_diag.h"
#include "app_clock/lvgl_clock_face_settings.h"
#include "app_clock/lvgl_time_settings.h"
#include "app_clock/lvgl_agent.h"
#include "app_clock/lvgl_notes.h"
#include "app_theme/app_theme.h"
#include "app_rtc/rtc.h"
#include "app_agent/agent.h"

#define TAG   "main"

static lv_obj_t *main_tileview;

void app_main(void)
{
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    screen_power_button_init();

    lv_display_t *disp = bsp_display_start();
    lv_indev_t *tp = bsp_display_get_input_dev();
    screen_power_set_display(disp);
    bsp_display_backlight_on();
    bsp_sdcard_mount();
    screen_power_idle_monitor_init();

    Audio_Play_Init();
    msg_driver_init();
    power_diag_init();

    app_rtc_init();

    bsp_display_lock(-1);

    app_theme_init();
    app_alarm_init();
    app_timer_init();
    app_agent_init();

    main_tileview = lv_tileview_create(NULL);
    /* Clock sits at col 1 so it has a tile on each side: device status to the
     * left (swipe right from the clock) and the settings chain to the right
     * (swipe left), matching the existing settings navigation direction. The
     * whole horizontal chain lives on row 1 so the timer tile can sit above
     * the clock at row 0, col 1 (swipe up from the clock, swipe down to
     * return - LV_DIR_TOP/BOTTOM move to row-1/row+1 respectively). */
    lv_obj_t *tile_device_status = lv_tileview_add_tile(main_tileview, 0, 1, LV_DIR_HOR);
    lv_obj_t *tile_clock = lv_tileview_add_tile(main_tileview, 1, 1, (lv_dir_t)(LV_DIR_HOR | LV_DIR_TOP | LV_DIR_BOTTOM));
    lv_obj_t *tile_alarm = lv_tileview_add_tile(main_tileview, 2, 1, LV_DIR_HOR);
    lv_obj_t *tile_alarm_volume = lv_tileview_add_tile(main_tileview, 3, 1, LV_DIR_HOR);
    lv_obj_t *tile_brightness = lv_tileview_add_tile(main_tileview, 4, 1, LV_DIR_HOR);
    lv_obj_t *tile_theme = lv_tileview_add_tile(main_tileview, 5, 1, LV_DIR_HOR);
    lv_obj_t *tile_clock_face = lv_tileview_add_tile(main_tileview, 6, 1, LV_DIR_HOR);
    lv_obj_t *tile_time = lv_tileview_add_tile(main_tileview, 7, 1, LV_DIR_HOR);
    lv_obj_t *tile_notes = lv_tileview_add_tile(main_tileview, 8, 1, LV_DIR_HOR);
    lv_obj_t *tile_timer = lv_tileview_add_tile(main_tileview, 1, 0, LV_DIR_BOTTOM);
    /* Swipe down from the clock (row 2) - the other primary, non-settings
     * direction, mirroring the timer's swipe-up placement at row 0. */
    lv_obj_t *tile_agent = lv_tileview_add_tile(main_tileview, 1, 2, LV_DIR_TOP);
    device_status_ui_screen_init(tile_device_status);
    clock_ui_screen_init(tile_clock);
    alarm_settings_ui_screen_init(tile_alarm);
    alarm_volume_ui_screen_init(tile_alarm_volume);
    screen_brightness_ui_screen_init(tile_brightness);
    theme_settings_ui_screen_init(tile_theme);
    clock_face_settings_ui_screen_init(tile_clock_face);
    time_settings_ui_screen_init(tile_time);
    notes_ui_screen_init(tile_notes);
    timer_ui_screen_init(tile_timer);
    agent_ui_screen_init(tile_agent);

    /* Tile (0,1) is device status now that clock moved to row 1; start on
     * the clock instead of changing the tileview's default landing spot. */
    lv_tileview_set_tile_by_index(main_tileview, 1, 1, LV_ANIM_OFF);

    lv_screen_load(main_tileview);

    bsp_display_unlock();
}