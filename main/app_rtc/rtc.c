#include "rtc.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

#define TAG "rtc"

static pcf85063a_dev_t s_dev;
static bool s_ready = false;

esp_err_t app_rtc_init(void)
{
    esp_err_t ret = pcf85063a_init(&s_dev, bsp_i2c_get_handle(), PCF85063A_ADDRESS);
    s_ready = (ret == ESP_OK);
    if (!s_ready) {
        ESP_LOGE(TAG, "Failed to init PCF85063A RTC");
    }
    return ret;
}

esp_err_t rtc_get_time(pcf85063a_datetime_t *out)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return pcf85063a_get_time_date(&s_dev, out);
}

esp_err_t rtc_set_time(const pcf85063a_datetime_t *in)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return pcf85063a_set_time_date(&s_dev, *in);
}
