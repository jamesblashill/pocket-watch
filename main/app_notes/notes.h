#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOTES_STATE_IDLE,
    NOTES_STATE_RECORDING,
    NOTES_STATE_PLAYING,
} notes_state_t;

typedef struct {
    char filename[64];      /* basename only, e.g. "note_20260816_143005.wav" */
    uint32_t duration_sec;
} note_entry_t;

/* Assumes bsp_sdcard_mount() has already run. Creates /sdcard/audio-notes if
 * missing and populates the initial list. */
void app_notes_init(void);

/* Re-scans the notes directory, newest first. Called automatically after a
 * recording finishes; call again after app_notes_delete() to pick up UI
 * changes, or any other time the on-disk list may have moved. */
void app_notes_refresh_list(void);
int app_notes_get_count(void);
const note_entry_t *app_notes_get_entry(int index);

/* No-ops unless currently idle/recording respectively. */
void app_notes_start_recording(void);
void app_notes_stop_recording(void);

/* Starts playing entry `index` from the current list. Fails with
 * ESP_ERR_INVALID_STATE if a recording or another playback is already
 * active - stop it first. */
esp_err_t app_notes_play(int index);
void app_notes_stop_playing(void);

/* Fails with ESP_ERR_INVALID_STATE while anything is recording/playing, so a
 * file already open on one task is never deleted out from under it. */
esp_err_t app_notes_delete(int index);

notes_state_t app_notes_get_state(void);

/* Seconds elapsed in the current recording/playback - 0 when idle. */
uint32_t app_notes_get_elapsed_sec(void);

/* Index (into the current list) currently playing, or -1 if none. */
int app_notes_get_playing_index(void);

#ifdef __cplusplus
}
#endif
