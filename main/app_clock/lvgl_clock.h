#pragma once

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_FACE_DIGITAL = 0,
    CLOCK_FACE_ANALOG,
} clock_face_t;

void clock_ui_screen_init(lv_obj_t *parent);

/* Sets the active clock face and persists the choice to NVS. */
void clock_ui_set_face(clock_face_t face);
clock_face_t clock_ui_get_face(void);

#ifdef __cplusplus
}
#endif
