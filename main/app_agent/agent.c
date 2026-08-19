#include "agent.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_transport_ws.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#include "bsp_board_extra.h"
#include "app_net/wifi_driver.h"
#include "app_net/wifi_credentials.h"
#include "app_net/agent_credentials.h"

#define TAG   "app_agent"

#define AGENT_SAMPLE_RATE       16000
#define AGENT_FRAME_MS          20
#define AGENT_FRAME_BYTES       (AGENT_SAMPLE_RATE * AGENT_FRAME_MS / 1000 * 2)  /* 16-bit mono */
#define AGENT_CONNECT_TIMEOUT_MS  15000

/* Jitter buffer between incoming WS audio and the I2S write, so a momentary
 * WiFi/network stall drains headroom instead of starving the DMA directly
 * (which is what caused audible dropouts before this buffer existed - the WS
 * task used to call Audio_Stream_Write() straight from the receive path).
 * Sized in wall-clock ms of 16-bit mono audio at AGENT_SAMPLE_RATE. */
#define AGENT_PLAYBACK_BUFFER_MS    400
#define AGENT_PLAYBACK_PRIME_MS     150
#define AGENT_PLAYBACK_BUFFER_BYTES (AGENT_SAMPLE_RATE * 2 * AGENT_PLAYBACK_BUFFER_MS / 1000)
#define AGENT_PLAYBACK_PRIME_BYTES  (AGENT_SAMPLE_RATE * 2 * AGENT_PLAYBACK_PRIME_MS / 1000)
#define AGENT_PLAYBACK_TRIGGER_BYTES  64

static agent_state_t s_state = AGENT_STATE_IDLE;
static char s_error_text[64];
static esp_websocket_client_handle_t s_ws_client;
static bool s_ws_started;
static TaskHandle_t s_mic_task;
static TaskHandle_t s_playback_task;
/* Created once at init and reused (reset) across responses/sessions - never
 * destroyed at runtime, so there's no window where a WS frame or the
 * playback task could touch a freed handle. */
static StreamBufferHandle_t s_playback_stream;
/* Set when response.done arrives: tells playback_task no more audio is
 * coming, so it should drain whatever's left in the buffer and then stop,
 * rather than waiting indefinitely for more. */
static volatile bool s_response_ending;
/* UI-only, see app_agent_is_thinking() in agent.h. */
static volatile bool s_thinking;
/* Diagnostics only, reset per response - see playback_task()'s completion
 * log. Not meant to stay in the codebase long-term. */
static uint32_t s_diag_bytes_fed;
static uint32_t s_diag_bytes_dropped;
static uint32_t s_diag_bytes_played;
static uint32_t s_diag_write_errors;
static esp_timer_handle_t s_connect_timeout_timer;
/* Fires the actual teardown one tick later than requested - see
 * schedule_teardown() for why this indirection exists. */
static esp_timer_handle_t s_teardown_defer_timer;
static agent_state_t s_pending_teardown_state;
/* True for the duration of agent_teardown() below. esp_websocket_client_stop()
 * can synchronously fire a WEBSOCKET_EVENT_DISCONNECTED (from the client's
 * own task, concurrently with the esp_timer task running this function) on
 * its way down, which would otherwise re-enter schedule_teardown() and queue
 * a redundant second teardown. */
static bool s_tearing_down;

static void agent_teardown(agent_state_t final_state)
{
    if (s_tearing_down) {
        return;
    }
    s_tearing_down = true;

    esp_timer_stop(s_connect_timeout_timer);

    /* Setting this first is what makes mic_capture_task's loop condition go
     * false and self-delete within its next ~20ms iteration. */
    s_state = final_state;
    s_thinking = false;

    if (s_ws_client) {
        if (s_ws_started) {
            esp_websocket_client_stop(s_ws_client);
        }
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
    s_ws_started = false;

    /* mic_capture_task and playback_task each poll s_state (already flipped
     * above) and self-delete within ~20-50ms of noticing, nulling their own
     * handle right before vTaskDelete(). Wait for that here before touching
     * the codec devices they were just using: closing a stream while the
     * other task is mid-write/mid-read on the same handle, from a different
     * task, is undefined behavior at the driver level - playback_task in
     * particular writes to the speaker in an almost continuous loop while a
     * response is active, so this isn't just theoretical, it's the reset
     * seen when Stop was pressed mid-response. Bounded so a stuck task
     * can't turn this into a new hang; 200ms is generous next to their
     * ~50ms max exit latency. */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(200);
    while ((s_mic_task || s_playback_task) && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* Audio_Stream_Close()/Audio_Capture_Close() are no-ops if never opened
     * this session (e.g. torn down while still CONNECTING). Note
     * Audio_Stream_* shares spk_codec_dev with the alarm/timer WAV playback
     * path in bsp_board_extra.c - an alarm firing mid-conversation is an
     * unhandled edge case, same as alarm/timer already sharing that device
     * with each other. */
    Audio_Stream_Close();
    Audio_Capture_Close();
    wifi_driver_release();

    s_tearing_down = false;
}

static void teardown_defer_cb(void *arg)
{
    agent_teardown(s_pending_teardown_state);
}

/* esp_websocket_client_stop()/_destroy() must not be called from inside the
 * client's own event callback (deadlocks waiting for its own task to exit).
 * Every teardown path in this file - including the ones triggered from
 * ws_event_handler() below - routes through here instead, which hops onto
 * the esp_timer task via a zero-delay one-shot timer before actually
 * tearing down. */
static void schedule_teardown(agent_state_t final_state, const char *error_text)
{
    if (s_state == AGENT_STATE_IDLE || s_tearing_down) {
        return;
    }
    if (error_text) {
        snprintf(s_error_text, sizeof(s_error_text), "%s", error_text);
    }
    s_pending_teardown_state = final_state;
    esp_timer_start_once(s_teardown_defer_timer, 0);
}

static void mic_capture_task(void *arg)
{
    static uint8_t frame[AGENT_FRAME_BYTES];

    while (s_state == AGENT_STATE_LISTENING || s_state == AGENT_STATE_SPEAKING) {
        if (s_state != AGENT_STATE_LISTENING) {
            /* Barge-in is deferred (v1) - don't stream mic audio while the
             * assistant is speaking, just idle until it's our turn again. */
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (Audio_Capture_Read(frame, sizeof(frame)) == ESP_OK &&
            s_ws_client && esp_websocket_client_is_connected(s_ws_client)) {
            esp_websocket_client_send_bin(s_ws_client, (const char *)frame, sizeof(frame), portMAX_DELAY);
        }
    }

    s_mic_task = NULL;
    vTaskDelete(NULL);
}

/* Drains s_playback_stream into the speaker at its own pace, decoupled from
 * how bursty the WS delivery is - see AGENT_PLAYBACK_BUFFER_MS above. Owns
 * closing the audio stream and handing control back to the mic, but only on
 * a natural, fully-drained completion; if it's cut short by a teardown
 * (state no longer SPEAKING) it leaves that to agent_teardown(), which
 * already closes the stream unconditionally. */
static void playback_task(void *arg)
{
    static uint8_t frame[AGENT_FRAME_BYTES];
    bool completed = false;

    while (!s_response_ending && s_state == AGENT_STATE_SPEAKING &&
           xStreamBufferBytesAvailable(s_playback_stream) < AGENT_PLAYBACK_PRIME_BYTES) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (s_state == AGENT_STATE_SPEAKING) {
        size_t n = xStreamBufferReceive(s_playback_stream, frame, sizeof(frame), pdMS_TO_TICKS(50));
        if (n > 0) {
            esp_err_t err = Audio_Stream_Write(frame, n);
            if (err == ESP_OK) {
                s_diag_bytes_played += n;
            } else {
                s_diag_write_errors++;
                ESP_LOGW(TAG, "Audio_Stream_Write failed: %d (n=%u)", err, (unsigned)n);
            }
        } else if (s_response_ending && xStreamBufferIsEmpty(s_playback_stream)) {
            completed = true;
            break;
        }
    }

    ESP_LOGI(TAG, "Playback diag: fed=%u dropped=%u played=%u write_errors=%u completed=%d",
             (unsigned)s_diag_bytes_fed, (unsigned)s_diag_bytes_dropped, (unsigned)s_diag_bytes_played,
             (unsigned)s_diag_write_errors, (int)completed);

    s_playback_task = NULL;
    if (completed) {
        Audio_Stream_Close();
        s_thinking = false;
        s_state = AGENT_STATE_LISTENING;
    }
    vTaskDelete(NULL);
}

static void send_session_start(void)
{
    char payload[96];
    int n = snprintf(payload, sizeof(payload),
                      "{\"type\":\"session.start\",\"sample_rate\":%d,\"format\":\"pcm16\"}",
                      AGENT_SAMPLE_RATE);
    esp_websocket_client_send_text(s_ws_client, payload, n, portMAX_DELAY);
}

static void on_session_ready(void)
{
    if (s_state != AGENT_STATE_CONNECTING) {
        return;
    }
    esp_timer_stop(s_connect_timeout_timer);

    if (Audio_Capture_Open(AGENT_SAMPLE_RATE) != ESP_OK) {
        schedule_teardown(AGENT_STATE_ERROR, "Mic init failed");
        return;
    }

    s_thinking = false;
    s_state = AGENT_STATE_LISTENING;
    xTaskCreate(mic_capture_task, "agent_mic", 4096, NULL, 5, &s_mic_task);
}

static void on_speech_stopped(void)
{
    if (s_state != AGENT_STATE_LISTENING) {
        return;
    }
    s_thinking = true;
}

static void on_response_started(void)
{
    if (s_state != AGENT_STATE_LISTENING) {
        return;
    }
    if (Audio_Stream_Open(AGENT_SAMPLE_RATE) != ESP_OK) {
        return;
    }
    xStreamBufferReset(s_playback_stream);
    s_response_ending = false;
    s_thinking = false;
    s_diag_bytes_fed = 0;
    s_diag_bytes_dropped = 0;
    s_diag_bytes_played = 0;
    s_diag_write_errors = 0;
    s_state = AGENT_STATE_SPEAKING;
    xTaskCreate(playback_task, "agent_playback", 4096, NULL, 5, &s_playback_task);
}

static void on_response_done(void)
{
    if (s_state != AGENT_STATE_SPEAKING) {
        return;
    }
    /* Don't close the stream here - playback_task is likely still draining
     * buffered audio. It closes and flips the state back once the buffer is
     * confirmed empty, so the tail of the response isn't clipped. */
    s_response_ending = true;
}

static void handle_control_message(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : NULL;

    if (type == NULL) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "session.ready") == 0) {
        on_session_ready();
    } else if (strcmp(type, "speech.stopped") == 0) {
        on_speech_stopped();
    } else if (strcmp(type, "response.started") == 0) {
        on_response_started();
    } else if (strcmp(type, "response.done") == 0) {
        on_response_done();
    } else if (strcmp(type, "error") == 0) {
        cJSON *msg_item = cJSON_GetObjectItem(root, "message");
        const char *msg = cJSON_IsString(msg_item) ? msg_item->valuestring : "Unknown error";
        ESP_LOGW(TAG, "Backend error: %s", msg);
        schedule_teardown(AGENT_STATE_ERROR, msg);
    } else if (strcmp(type, "session.end") == 0) {
        cJSON *reason_item = cJSON_GetObjectItem(root, "reason");
        const char *reason = cJSON_IsString(reason_item) ? reason_item->valuestring : "server";
        ESP_LOGI(TAG, "Backend ended session: %s", reason);
        schedule_teardown(AGENT_STATE_IDLE, NULL);
    }

    cJSON_Delete(root);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        send_session_start();
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
            if (s_state == AGENT_STATE_SPEAKING) {
                /* Feeds the jitter buffer that playback_task drains at its
                 * own pace - see AGENT_PLAYBACK_BUFFER_MS.
                 *
                 * Raw PCM has no framing to resync on, so dropping even one
                 * byte out of the middle of an in-progress response
                 * permanently shifts every sample after it - audible as
                 * garbled/static for the rest of the response, not a clean
                 * gap. (Also: this client fragments any delta bigger than
                 * its rx buffer into several DATA events - a per-event
                 * drop is still a mid-stream drop.) So never partially
                 * accept a chunk: retry in short bounded waits until either
                 * all of it is in, or s_state stops being SPEAKING, which
                 * only happens when a teardown is already discarding the
                 * rest of this response anyway - that's the only point
                 * where dropping the tail is harmless.
                 *
                 * Each wait is short (not one long block) because this
                 * callback runs synchronously on the WS client's own task
                 * (esp_websocket_client dispatches inline), and
                 * esp_websocket_client_stop() waits on that same task with
                 * portMAX_DELAY - a long wait here would directly delay
                 * every app_agent_end_session() call. */
                const uint8_t *p = (const uint8_t *)data->data_ptr;
                size_t remaining = (size_t)data->data_len;
                while (remaining > 0 && s_state == AGENT_STATE_SPEAKING) {
                    size_t sent = xStreamBufferSend(s_playback_stream, p, remaining, pdMS_TO_TICKS(20));
                    p += sent;
                    remaining -= sent;
                    s_diag_bytes_fed += sent;
                }
                s_diag_bytes_dropped += remaining;
            }
        } else if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
            handle_control_message(data->data_ptr, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "WS error");
        schedule_teardown(AGENT_STATE_ERROR, "Connection error");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        schedule_teardown(AGENT_STATE_ERROR, "Disconnected");
        break;
    default:
        break;
    }
}

static void connect_timeout_cb(void *arg)
{
    ESP_LOGW(TAG, "Connect timed out");
    schedule_teardown(AGENT_STATE_ERROR, "Connect timed out");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == AGENT_STATE_CONNECTING || s_state == AGENT_STATE_LISTENING ||
            s_state == AGENT_STATE_SPEAKING) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        /* Only start the websocket client once the STA interface actually has
         * an IP/route - starting it right after esp_wifi_connect() races the
         * association/DHCP handshake and esp-tls fails immediately with
         * "Host is unreachable" (no route yet) rather than timing out. */
        if (s_state == AGENT_STATE_CONNECTING && s_ws_client && !s_ws_started) {
            s_ws_started = true;
            esp_websocket_client_start(s_ws_client);
        }
    }
}

void app_agent_init(void)
{
    wifi_driver_init_once();

    s_playback_stream = xStreamBufferCreate(AGENT_PLAYBACK_BUFFER_BYTES, AGENT_PLAYBACK_TRIGGER_BYTES);
    assert(s_playback_stream);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    const esp_timer_create_args_t connect_timeout_args = {
        .callback = connect_timeout_cb,
        .name = "agent_connect_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&connect_timeout_args, &s_connect_timeout_timer));

    const esp_timer_create_args_t teardown_defer_args = {
        .callback = teardown_defer_cb,
        .name = "agent_teardown_defer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&teardown_defer_args, &s_teardown_defer_timer));
}

void app_agent_start_session(void)
{
    /* s_state flips to IDLE at the very start of agent_teardown(), well
     * before the teardown itself finishes - esp_websocket_client_stop() can
     * take up to ~1s (the vendored client's own poll cadence), and
     * destroy()/wifi_driver_release() run after that. Checking only
     * s_state here would let a session start while an old teardown is
     * still in flight, which then destroys *this* session's fresh
     * s_ws_client (thinking it's the old one) and releases the WiFi ref
     * this session just acquired - leaving it stuck in CONNECTING with a
     * NULL client until the 15s connect timeout bails it out. Gating on
     * s_tearing_down too (already the guard schedule_teardown() uses)
     * closes that window. */
    if (s_tearing_down || (s_state != AGENT_STATE_IDLE && s_state != AGENT_STATE_ERROR)) {
        return;
    }
    s_error_text[0] = '\0';
    s_state = AGENT_STATE_CONNECTING;

    wifi_driver_acquire();
    esp_wifi_connect();

    esp_websocket_client_config_t ws_cfg = {
        .uri = AGENT_BACKEND_WS_URL,
        .headers = "Authorization: Bearer " AGENT_DEVICE_SHARED_SECRET "\r\n",
    };
    s_ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    /* Actually started once IP_EVENT_STA_GOT_IP fires - see wifi_event_handler() -
     * starting it here would race association/DHCP and fail immediately with
     * "Host is unreachable". */

    esp_timer_start_once(s_connect_timeout_timer, (uint64_t)AGENT_CONNECT_TIMEOUT_MS * 1000);
}

void app_agent_end_session(void)
{
    schedule_teardown(AGENT_STATE_IDLE, NULL);
}

agent_state_t app_agent_get_state(void)
{
    return s_state;
}

const char *app_agent_get_error_text(void)
{
    return s_error_text;
}

bool app_agent_is_stopping(void)
{
    return s_tearing_down;
}

bool app_agent_is_thinking(void)
{
    return s_thinking;
}
