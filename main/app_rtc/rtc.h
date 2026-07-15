#pragma once

#include "esp_err.h"
#include "pcf85063a.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_rtc_init(void);
esp_err_t rtc_get_time(pcf85063a_datetime_t *out);
esp_err_t rtc_set_time(const pcf85063a_datetime_t *in);

#ifdef __cplusplus
}
#endif
