#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AGENT_STATE_IDLE,
    AGENT_STATE_CONNECTING,
    AGENT_STATE_LISTENING,
    AGENT_STATE_SPEAKING,
    AGENT_STATE_ERROR,
} agent_state_t;

/* Idempotent setup: registers WiFi event handlers and the connect-timeout
 * timer. Does not touch the radio, mic, or speaker - those are only powered
 * once a session actually starts. */
void app_agent_init(void);

/* Starts a voice session: acquires WiFi, connects to the backend, and once
 * the backend signals it's ready, starts streaming mic audio. Ignored if a
 * session is already active. */
void app_agent_start_session(void);

/* Ends the current session (if any) and returns to idle. Safe to call from
 * any state. */
void app_agent_end_session(void);

agent_state_t app_agent_get_state(void);

/* Valid only in AGENT_STATE_ERROR; empty string otherwise. */
const char *app_agent_get_error_text(void);

/* True while a stop/teardown is still finishing in the background - state
 * has already flipped to IDLE (or ERROR), but the WS client's own stop
 * handshake and WiFi release haven't completed yet, which can take close to
 * a second. UI-only signal: start/end session calls are already safe to
 * call regardless (they no-op appropriately), this just lets the UI show
 * that something's still happening instead of looking stuck. */
bool app_agent_is_stopping(void);

/* True from the moment the backend reports the user stopped talking until
 * the response actually starts (or the session ends/errors) - only
 * meaningful while AGENT_STATE_LISTENING. Lets the UI distinguish "still
 * listening" from "waiting on the model," which otherwise look identical. */
bool app_agent_is_thinking(void);

#ifdef __cplusplus
}
#endif
