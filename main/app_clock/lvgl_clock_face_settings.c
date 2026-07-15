#include "lvgl_clock_face_settings.h"
#include "lvgl_clock.h"
#include "app_theme/app_theme.h"

#define TAG   "clock_face_settings"

static const char *s_face_map[] = {"Digital", "Analog", ""};

static void face_buttonmatrix_event_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
    clock_ui_set_face(id == 1 ? CLOCK_FACE_ANALOG : CLOCK_FACE_DIGITAL);
}

void clock_face_settings_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 28, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_label_set_text(title, "Clock Face");

    lv_obj_t *btnm = lv_buttonmatrix_create(parent);
    lv_obj_add_style(btnm, app_theme_bg_style(), 0);
    lv_buttonmatrix_set_map(btnm, s_face_map);
    lv_buttonmatrix_set_button_ctrl_all(btnm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(btnm, true);
    lv_obj_set_size(btnm, 260, 60);
    /* A buttonmatrix isn't scrollable itself, so without this a slight
     * finger drift mid-tap would bubble up and swipe the tileview. */
    lv_obj_remove_flag(btnm, LV_OBJ_FLAG_SCROLL_CHAIN);

    uint32_t selected = (clock_ui_get_face() == CLOCK_FACE_ANALOG) ? 1 : 0;
    lv_buttonmatrix_set_button_ctrl(btnm, selected, LV_BUTTONMATRIX_CTRL_CHECKED);

    lv_obj_add_event_cb(btnm, face_buttonmatrix_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
