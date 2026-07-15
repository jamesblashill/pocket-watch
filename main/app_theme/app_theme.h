#pragma once

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_THEME_LIGHT = 0,
    APP_THEME_DARK,
} app_theme_mode_t;

/* Loads the saved mode from NVS (defaults to light) and prepares the shared
 * background/text styles. Must be called once before any screen is built. */
void app_theme_init(void);

app_theme_mode_t app_theme_get_mode(void);

/* Updates the shared styles in place and pushes the change out to every
 * object already wearing them, then persists the choice to NVS. */
void app_theme_set_mode(app_theme_mode_t mode);

/* Shared styles: add these to a screen's root container and its labels so
 * they follow the active theme. Backgrounds/text created before the first
 * app_theme_set_mode() call still pick up later changes since the style
 * object itself is mutated rather than replaced. */
lv_style_t *app_theme_bg_style(void);
lv_style_t *app_theme_text_style(void);

#ifdef __cplusplus
}
#endif
