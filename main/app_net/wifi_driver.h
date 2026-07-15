#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time bring-up of the WiFi driver plumbing (netif, default event loop,
 * STA netif, wifi_init, STA mode). Does NOT start the radio. Safe to call
 * from multiple independent features - only the first call does any work. */
void wifi_driver_init_once(void);

/* Ref-counted radio on/off, shared by every feature that needs WiFi (the
 * scan screen, time sync, ...). The radio actually starts on the first
 * acquire() and actually stops on the release() that drops the count back
 * to zero, so unrelated features can use WiFi concurrently without
 * stepping on each other. Every acquire() must be matched by a release(). */
esp_err_t wifi_driver_acquire(void);
esp_err_t wifi_driver_release(void);

#ifdef __cplusplus
}
#endif
