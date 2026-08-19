#include "lvgl_agent.h"
#include "app_agent/agent.h"
#include "app_theme/app_theme.h"

static lv_obj_t *s_status_label;
static lv_obj_t *s_btn_label;

static void update_ui(void)
{
    agent_state_t state = app_agent_get_state();

    switch (state) {
    case AGENT_STATE_IDLE:
        lv_label_set_text(s_status_label, "Tap to talk");
        lv_label_set_text(s_btn_label, "Talk");
        break;
    case AGENT_STATE_CONNECTING:
        lv_label_set_text(s_status_label, "Connecting...");
        lv_label_set_text(s_btn_label, "Stop");
        break;
    case AGENT_STATE_LISTENING:
        lv_label_set_text(s_status_label, app_agent_is_thinking() ? "Thinking..." : "Listening...");
        lv_label_set_text(s_btn_label, "Stop");
        break;
    case AGENT_STATE_SPEAKING:
        lv_label_set_text(s_status_label, "Speaking...");
        lv_label_set_text(s_btn_label, "Stop");
        break;
    case AGENT_STATE_ERROR:
        lv_label_set_text_fmt(s_status_label, "Error: %s", app_agent_get_error_text());
        lv_label_set_text(s_btn_label, "Retry");
        break;
    }

    /* Overrides the state-driven status text above (not the button label,
     * which already reflects what tapping would actually do - start fresh -
     * since state itself has already flipped to IDLE by this point). Covers
     * the ~1s tail after Stop where the WS client is still finishing its
     * own shutdown handshake in the background - see
     * app_agent_is_stopping(). Without this the screen jumps straight to
     * "Tap to talk" and a tap in that window is silently absorbed, which
     * reads as the button not working. */
    if (app_agent_is_stopping()) {
        lv_label_set_text(s_status_label, "Stopping...");
    }
}

static void agent_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    agent_state_t state = app_agent_get_state();
    if (state == AGENT_STATE_IDLE || state == AGENT_STATE_ERROR) {
        app_agent_start_session();
    } else {
        app_agent_end_session();
    }
    update_ui();
}

static void agent_poll_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    update_ui();
}

void agent_ui_screen_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 20, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_label_set_text(title, "Assistant");

    s_status_label = lv_label_create(parent);
    lv_obj_add_style(s_status_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_24, 0);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_pad_left(btn, 28, 0);
    lv_obj_set_style_pad_right(btn, 28, 0);
    lv_obj_set_style_pad_top(btn, 16, 0);
    lv_obj_set_style_pad_bottom(btn, 16, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(btn, agent_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_btn_label = lv_label_create(btn);
    lv_obj_set_style_text_font(s_btn_label, &lv_font_montserrat_24, 0);

    update_ui();
    lv_timer_create(agent_poll_timer_cb, 200, NULL);
}
