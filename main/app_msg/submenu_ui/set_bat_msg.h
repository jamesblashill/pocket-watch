#pragma once

#include "bsp/esp-bsp.h"
#include "bq27220.h"

#ifdef __cplusplus
extern "C" {
#endif

bq27220_handle_t bq27220_drv_init(void);

/* Returns the handle created by bq27220_drv_init(), or NULL if that hasn't
 * run yet. Lets other screens (e.g. the device status tile) read battery
 * data without creating a second handle for the same physical gauge. */
bq27220_handle_t bq27220_drv_get_handle(void);

#ifdef __cplusplus
}
#endif
