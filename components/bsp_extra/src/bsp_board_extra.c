/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"


#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

static const char *TAG = "bsp_extra_board";

static uint8_t Volume = 80;
esp_asp_handle_t handle = NULL;
esp_codec_dev_handle_t spk_codec_dev = NULL;
/* Lazily created on first Audio_Capture_Open() - bsp_audio_codec_microphone_init()
 * is otherwise unused today, unlike the speaker path which Audio_Play_Init()
 * sets up eagerly at boot. */
static esp_codec_dev_handle_t mic_codec_dev = NULL;

/* Kept open only for the duration of a play - see Audio_Play_Music() /
 * Audio_Stop_Play() and the ESP_ASP_STATE_FINISHED case below. Opening the
 * codec is what enables the underlying I2S channel (see bsp_audio_init()),
 * so leaving it open for the whole runtime (the previous behavior) pinned
 * the chip out of light sleep permanently. */
static const esp_codec_dev_sample_info_t k_speaker_fs = {
    .bits_per_sample = 32,
    .channel = 2,
    .sample_rate = 16000,
};

static void Audio_PA_EN(void)
{
    //esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4 , 1);
}
static void Audio_PA_DIS(void)
{
    //esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4 , 0);
}


static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(dev, data, data_size);
    return 0;
}

static int mock_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) 
    {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGI(TAG, "Get info, rate:%d, channels:%d, bits:%d ,bitrate = %d", info.sample_rate, info.channels, info.bits,info.bitrate);
    } 
    else if (event->type == ESP_ASP_EVENT_TYPE_STATE) 
    {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);

        ESP_LOGI(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));
        if(st == ESP_ASP_STATE_FINISHED)
        {
            ESP_LOGI(TAG,"play done");
            Audio_PA_DIS();
            esp_codec_dev_close(spk_codec_dev);
        }
    }
    return 0;
}



static esp_asp_handle_t create_simple_player(const esp_asp_data_func in_cb, void *in_ctx,const esp_asp_event_func event_cb, void *event_ctx)
{
    esp_asp_cfg_t cfg = {
        .in.cb = in_cb,
        .in.user_ctx = in_ctx,
        .out.cb = out_data_callback,
        .out.user_ctx = spk_codec_dev,
        .task_prio = 5,
    };

    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    err = esp_audio_simple_player_set_event(handle, event_cb, event_ctx);
    return handle;
}

void Audio_Play_Init(void) 
{
    spk_codec_dev = bsp_audio_codec_speaker_init();
    assert(spk_codec_dev);
    
    handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    Volume_Adjustment(55);
}



void Volume_Adjustment(uint8_t Vol)
{
    Volume = Vol;
    esp_codec_dev_set_out_vol(spk_codec_dev, Volume);
}
uint8_t get_audio_volume(void)
{
    return Volume;
}

esp_gmf_err_t Audio_Play_Music(const char* url)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    Audio_PA_DIS();
    esp_codec_dev_sample_info_t fs = k_speaker_fs;
    esp_codec_dev_open(spk_codec_dev, &fs);
    ret = esp_audio_simple_player_stop(handle);
    //vTaskDelay(200 / portTICK_PERIOD_MS);
    ret = esp_audio_simple_player_run(handle, url, NULL);
    Audio_PA_EN();
    return ret;
}

esp_gmf_err_t Audio_Stop_Play(void)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    Audio_PA_DIS();
    esp_audio_simple_player_stop(handle);
    esp_codec_dev_close(spk_codec_dev);
    return ret;
}

esp_gmf_err_t Audio_Resume_Play(void)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_audio_simple_player_resume(handle);
    Audio_PA_EN();
    return ret;
}
esp_gmf_err_t Audio_Pause_Play(void)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    Audio_PA_DIS();
    esp_audio_simple_player_pause(handle);
    return ret;
}
esp_asp_state_t Audio_Get_Current_State(void) 
{
    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(handle, &state);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE("AUDIO", "Get state failed: %d", err);
        return ESP_ASP_STATE_ERROR;
    }
    return state;
}
void Audio_Play_Deinit(void)
{
    Audio_PA_DIS();
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_0));
    esp_audio_simple_player_destroy(handle);
}

/* --- Raw PCM16 mono streaming capture/playback, for the voice agent -----
 * Distinct from the WAV/URL-based Audio_Play_Music() path above: these write
 * and read live network-sourced frames directly against the codec devices,
 * with no esp_audio_simple_player/esp_gmf pipeline in between. Same
 * open-only-while-active/close-when-done discipline as the rest of this
 * file, so the I2S channel isn't held awake outside an active session. */

esp_err_t Audio_Capture_Open(uint32_t sample_rate)
{
    if (mic_codec_dev == NULL) {
        mic_codec_dev = bsp_audio_codec_microphone_init();
        if (mic_codec_dev == NULL) {
            return ESP_FAIL;
        }
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = sample_rate,
    };
    esp_err_t err = esp_codec_dev_open(mic_codec_dev, &fs);
    if (err == ESP_OK) {
        /* ES7210 hardware gain is set to 30dB by default (out of a 37.5dB
         * ceiling) - push it to max, since captured audio has been coming
         * through quiet. Gain can only be set while the device is open, so
         * this has to happen here rather than at bsp_audio_codec_microphone_init(). */
        esp_codec_dev_set_in_gain(mic_codec_dev, 37.5f);
    }
    return err;
}

esp_err_t Audio_Capture_Read(uint8_t *buf, size_t len)
{
    return esp_codec_dev_read(mic_codec_dev, buf, len);
}

void Audio_Capture_Close(void)
{
    if (mic_codec_dev != NULL) {
        esp_codec_dev_close(mic_codec_dev);
    }
}

esp_err_t Audio_Stream_Open(uint32_t sample_rate)
{
    Audio_PA_DIS();
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = sample_rate,
    };
    esp_err_t err = esp_codec_dev_open(spk_codec_dev, &fs);
    if (err == ESP_OK) {
        Audio_PA_EN();
    }
    return err;
}

esp_err_t Audio_Stream_Write(const uint8_t *buf, size_t len)
{
    return esp_codec_dev_write(spk_codec_dev, (void *)buf, len);
}

void Audio_Stream_Close(void)
{
    Audio_PA_DIS();
    esp_codec_dev_close(spk_codec_dev);
}