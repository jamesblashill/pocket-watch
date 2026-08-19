#include "lvgl_notes.h"
#include "app_notes/notes.h"
#include "app_theme/app_theme.h"

#include <stdio.h>

static lv_obj_t *s_status_label;
static lv_obj_t *s_record_btn_label;
static lv_obj_t *s_list;
/* Last row tapped (played or currently playing) - what Delete acts on.
 * -1 means nothing selected yet. */
static int s_selected_index = -1;
static notes_state_t s_last_state = NOTES_STATE_IDLE;

static void format_display_name(const char *filename, uint32_t duration_sec, char *out, size_t out_size)
{
    unsigned y, mo, d, h, mi, s;
    if (sscanf(filename, "note_%4u%2u%2u_%2u%2u%2u.wav", &y, &mo, &d, &h, &mi, &s) == 6) {
        snprintf(out, out_size, "%04u-%02u-%02u %02u:%02u  (%u:%02u)",
                 y, mo, d, h, mi, (unsigned)(duration_sec / 60), (unsigned)(duration_sec % 60));
    } else {
        snprintf(out, out_size, "%s  (%u:%02u)", filename,
                 (unsigned)(duration_sec / 60), (unsigned)(duration_sec % 60));
    }
}

static void row_event_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    notes_state_t state = app_notes_get_state();

    if (state == NOTES_STATE_PLAYING) {
        /* Tapping any row while one is playing just stops it - matches the
         * "one active operation at a time" model the record/play state
         * machine already enforces. Tap again to start a different note. */
        app_notes_stop_playing();
    } else if (state == NOTES_STATE_IDLE) {
        if (app_notes_play(index) == ESP_OK) {
            s_selected_index = index;
        }
    }
}

static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    int count = app_notes_get_count();
    int playing_index = app_notes_get_playing_index();

    if (count == 0) {
        lv_list_add_text(s_list, "No notes yet");
        return;
    }

    for (int i = 0; i < count; i++) {
        const note_entry_t *entry = app_notes_get_entry(i);
        char text[80];
        format_display_name(entry->filename, entry->duration_sec, text, sizeof(text));

        const char *icon = (playing_index == i) ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO;
        lv_obj_t *btn = lv_list_add_button(s_list, icon, text);
        lv_obj_add_event_cb(btn, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void update_ui(void)
{
    notes_state_t state = app_notes_get_state();
    uint32_t elapsed = app_notes_get_elapsed_sec();

    switch (state) {
    case NOTES_STATE_RECORDING:
        lv_label_set_text_fmt(s_status_label, "Recording... %u:%02u", (unsigned)(elapsed / 60), (unsigned)(elapsed % 60));
        lv_label_set_text(s_record_btn_label, "Stop");
        break;
    case NOTES_STATE_PLAYING:
        lv_label_set_text_fmt(s_status_label, "Playing... %u:%02u", (unsigned)(elapsed / 60), (unsigned)(elapsed % 60));
        lv_label_set_text(s_record_btn_label, "Record");
        break;
    case NOTES_STATE_IDLE:
    default:
        lv_label_set_text(s_status_label, "Tap Record to start");
        lv_label_set_text(s_record_btn_label, "Record");
        break;
    }
}

static void record_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    notes_state_t state = app_notes_get_state();
    if (state == NOTES_STATE_IDLE) {
        app_notes_start_recording();
    } else if (state == NOTES_STATE_RECORDING) {
        app_notes_stop_recording();
    }
    update_ui();
}

static void delete_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_selected_index >= 0 && app_notes_get_state() == NOTES_STATE_IDLE) {
        app_notes_delete(s_selected_index);
        s_selected_index = -1;
        rebuild_list();
    }
}

static void notes_poll_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    notes_state_t state = app_notes_get_state();

    /* Both recording and playback finish on a background task and flip the
     * state back to IDLE themselves - this is what notices that transition
     * and refreshes the on-screen list (new note appears / stopped icon
     * reverts) without the UI having to poll the filesystem itself. */
    if (state != s_last_state && state == NOTES_STATE_IDLE) {
        rebuild_list();
    }
    s_last_state = state;

    update_ui();
}

void notes_ui_screen_init(lv_obj_t *parent)
{
    app_notes_init();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_set_style_pad_top(parent, 16, 0);
    lv_obj_add_style(parent, app_theme_bg_style(), 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_add_style(title, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_label_set_text(title, "Voice Notes");

    s_status_label = lv_label_create(parent);
    lv_obj_add_style(s_status_label, app_theme_text_style(), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);

    lv_obj_t *btn_row = lv_obj_create(parent);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(92), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *record_btn = lv_button_create(btn_row);
    lv_obj_set_style_pad_left(record_btn, 24, 0);
    lv_obj_set_style_pad_right(record_btn, 24, 0);
    lv_obj_set_style_pad_top(record_btn, 12, 0);
    lv_obj_set_style_pad_bottom(record_btn, 12, 0);
    lv_obj_add_event_cb(record_btn, record_btn_event_cb, LV_EVENT_CLICKED, NULL);
    s_record_btn_label = lv_label_create(record_btn);
    lv_obj_set_style_text_font(s_record_btn_label, &lv_font_montserrat_20, 0);

    lv_obj_t *delete_btn = lv_button_create(btn_row);
    lv_obj_set_style_pad_left(delete_btn, 24, 0);
    lv_obj_set_style_pad_right(delete_btn, 24, 0);
    lv_obj_set_style_pad_top(delete_btn, 12, 0);
    lv_obj_set_style_pad_bottom(delete_btn, 12, 0);
    lv_obj_add_event_cb(delete_btn, delete_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_btn_label = lv_label_create(delete_btn);
    lv_obj_set_style_text_font(delete_btn_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(delete_btn_label, "Delete");

    s_list = lv_list_create(parent);
    lv_obj_set_width(s_list, lv_pct(92));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_CHAIN);

    rebuild_list();
    update_ui();
    lv_timer_create(notes_poll_timer_cb, 200, NULL);
}
