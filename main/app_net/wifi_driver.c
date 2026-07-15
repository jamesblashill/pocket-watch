#include "wifi_driver.h"

#include <stdbool.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static int s_refcount;

void wifi_driver_init_once(void)
{
    static bool s_initialized = false;
    if (s_initialized) {
        return;
    }
    s_initialized = true;

    s_lock = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

esp_err_t wifi_driver_acquire(void)
{
    wifi_driver_init_once();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_refcount == 0) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        s_refcount++;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t wifi_driver_release(void)
{
    wifi_driver_init_once();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_refcount > 0) {
        s_refcount--;
        if (s_refcount == 0) {
            esp_wifi_disconnect();
            err = esp_wifi_stop();
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}
