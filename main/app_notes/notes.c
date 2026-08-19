#include "notes.h"

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_board_extra.h"
#include "app_rtc/rtc.h"

#define TAG   "app_notes"

#define NOTES_DIR            "/sdcard/audio-notes"
#define NOTES_SAMPLE_RATE    16000
#define NOTES_FRAME_BYTES    640          /* 20ms @ 16kHz/16-bit/mono */
#define NOTES_MAX_ENTRIES    200
#define WAV_HEADER_BYTES     44

static note_entry_t s_entries[NOTES_MAX_ENTRIES];
static int s_entry_count;

static volatile notes_state_t s_state = NOTES_STATE_IDLE;
static volatile uint32_t s_elapsed_bytes;
static volatile bool s_stop_requested;
static volatile int s_playing_index = -1;
static TaskHandle_t s_task;
/* Filenames handed to record_task()/play_task() at spawn time - read only
 * after the task starts, so a refresh racing the *other* op can't matter
 * (recording and playback are mutually exclusive via s_state anyway). */
static char s_pending_filename[64];

static int compare_desc(const void *a, const void *b)
{
    return strcmp(((const note_entry_t *)b)->filename, ((const note_entry_t *)a)->filename);
}

void app_notes_refresh_list(void)
{
    s_entry_count = 0;

    DIR *dir = opendir(NOTES_DIR);
    if (!dir) {
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_entry_count < NOTES_MAX_ENTRIES) {
        size_t len = strlen(ent->d_name);
        if (len < 5 || len >= sizeof(s_entries[0].filename) || strcmp(ent->d_name + len - 4, ".wav") != 0) {
            continue;
        }

        char path[128];
        snprintf(path, sizeof(path), "%s/%.100s", NOTES_DIR, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || (size_t)st.st_size <= WAV_HEADER_BYTES) {
            continue;
        }

        note_entry_t *e = &s_entries[s_entry_count++];
        snprintf(e->filename, sizeof(e->filename), "%.63s", ent->d_name);
        e->duration_sec = (uint32_t)((st.st_size - WAV_HEADER_BYTES) / (NOTES_SAMPLE_RATE * 2));
    }
    closedir(dir);

    qsort(s_entries, s_entry_count, sizeof(note_entry_t), compare_desc);
}

void app_notes_init(void)
{
    mkdir(NOTES_DIR, 0775);   /* no-op (EEXIST) if it already exists */
    app_notes_refresh_list();
}

int app_notes_get_count(void)
{
    return s_entry_count;
}

const note_entry_t *app_notes_get_entry(int index)
{
    if (index < 0 || index >= s_entry_count) {
        return NULL;
    }
    return &s_entries[index];
}

static void write_wav_header(FILE *f, uint32_t data_bytes)
{
    uint32_t byte_rate = NOTES_SAMPLE_RATE * 2;
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    uint16_t audio_format = 1;   /* PCM */
    uint16_t num_channels = 1;
    uint32_t sample_rate = NOTES_SAMPLE_RATE;
    uint32_t riff_size = 36 + data_bytes;
    uint32_t fmt_size = 16;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

static void record_task(void *arg)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, s_pending_filename);

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create %s", path);
        s_state = NOTES_STATE_IDLE;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    write_wav_header(f, 0);   /* placeholder sizes, patched below once known */

    uint32_t data_bytes = 0;

    if (Audio_Capture_Open(NOTES_SAMPLE_RATE) == ESP_OK) {
        uint8_t frame[NOTES_FRAME_BYTES];
        while (!s_stop_requested) {
            if (Audio_Capture_Read(frame, sizeof(frame)) != ESP_OK) {
                break;
            }
            fwrite(frame, 1, sizeof(frame), f);
            data_bytes += sizeof(frame);
            s_elapsed_bytes = data_bytes;
        }
        Audio_Capture_Close();
    } else {
        ESP_LOGE(TAG, "Mic open failed");
    }

    write_wav_header(f, data_bytes);
    fclose(f);

    if (data_bytes == 0) {
        remove(path);   /* stopped instantly / mic never opened - no empty file left behind */
    } else {
        app_notes_refresh_list();
    }

    s_state = NOTES_STATE_IDLE;
    s_task = NULL;
    vTaskDelete(NULL);
}

void app_notes_start_recording(void)
{
    if (s_state != NOTES_STATE_IDLE) {
        return;
    }

    pcf85063a_datetime_t now;
    if (rtc_get_time(&now) == ESP_OK && now.year >= 2020) {
        snprintf(s_pending_filename, sizeof(s_pending_filename),
                 "note_%04u%02u%02u_%02u%02u%02u.wav",
                 (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
                 (unsigned)now.hour, (unsigned)now.min, (unsigned)now.sec);
    } else {
        /* RTC unset/unavailable - fall back to a monotonic-ish name so
         * recordings still sort in creation order and never collide. */
        snprintf(s_pending_filename, sizeof(s_pending_filename),
                 "note_%lld.wav", (long long)(esp_timer_get_time() / 1000000));
    }

    s_stop_requested = false;
    s_elapsed_bytes = 0;
    s_state = NOTES_STATE_RECORDING;
    xTaskCreate(record_task, "notes_rec", 4096, NULL, 5, &s_task);
}

void app_notes_stop_recording(void)
{
    if (s_state == NOTES_STATE_RECORDING) {
        s_stop_requested = true;
    }
}

static void play_task(void *arg)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, s_pending_filename);

    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, WAV_HEADER_BYTES, SEEK_SET);

        if (Audio_Stream_Open(NOTES_SAMPLE_RATE) == ESP_OK) {
            uint8_t frame[NOTES_FRAME_BYTES];
            uint32_t played_bytes = 0;
            size_t n;
            while (!s_stop_requested && (n = fread(frame, 1, sizeof(frame), f)) > 0) {
                Audio_Stream_Write(frame, n);
                played_bytes += n;
                s_elapsed_bytes = played_bytes;
            }
            Audio_Stream_Close();
        } else {
            ESP_LOGE(TAG, "Speaker open failed");
        }
        fclose(f);
    } else {
        ESP_LOGE(TAG, "Failed to open %s", path);
    }

    s_playing_index = -1;
    s_state = NOTES_STATE_IDLE;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t app_notes_play(int index)
{
    if (index < 0 || index >= s_entry_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != NOTES_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(s_pending_filename, sizeof(s_pending_filename), "%s", s_entries[index].filename);
    s_stop_requested = false;
    s_elapsed_bytes = 0;
    s_playing_index = index;
    s_state = NOTES_STATE_PLAYING;
    xTaskCreate(play_task, "notes_play", 4096, NULL, 5, &s_task);
    return ESP_OK;
}

void app_notes_stop_playing(void)
{
    if (s_state == NOTES_STATE_PLAYING) {
        s_stop_requested = true;
    }
}

esp_err_t app_notes_delete(int index)
{
    if (index < 0 || index >= s_entry_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != NOTES_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, s_entries[index].filename);
    if (remove(path) != 0) {
        return ESP_FAIL;
    }
    app_notes_refresh_list();
    return ESP_OK;
}

notes_state_t app_notes_get_state(void)
{
    return s_state;
}

uint32_t app_notes_get_elapsed_sec(void)
{
    return s_elapsed_bytes / (NOTES_SAMPLE_RATE * 2);
}

int app_notes_get_playing_index(void)
{
    return s_playing_index;
}
