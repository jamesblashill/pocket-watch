#include "set_wifi_service.h"
#include <stdio.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "app_net/wifi_driver.h"

static lv_obj_t *list1 = NULL;
static lv_obj_t * spinner  = NULL;


static const char *TAG = "wifi_scan";
static EventGroupHandle_t s_wifi_event_group;
static void wifi_scan_ui_task(void *arg);

#define WIFI_STARE_SCAN BIT0

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected from AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}


void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    wifi_driver_init_once();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    xTaskCreate(wifi_scan_ui_task, "wifi_scan_ui_task", 4096, NULL, 5, NULL);
}

static void do_wifi_scan(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi scan...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true)); // true = 阻塞直到扫描完成

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d networks", ap_count);

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list) {
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
        for (int i = 0; i < ap_count; i++) {
            ESP_LOGI(TAG, "%2d: SSID=%-32s  RSSI=%d  CH=%d",
                     i + 1,
                     (char *)ap_list[i].ssid,
                     ap_list[i].rssi,
                     ap_list[i].primary);
        }

        bsp_display_lock(-1);
        
        for (int i = 0; i < ap_count; i++) 
        {
            char label[64];
            snprintf(label, sizeof(label), "%s  (%d dBm)",
                        (char *)ap_list[i].ssid, ap_list[i].rssi);


            lv_obj_t *btn = lv_list_add_button(list1, LV_SYMBOL_WIFI, label);

        }
        lv_obj_add_flag(spinner,LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();

        free(ap_list);
    }
}


static void wifi_scan_ui_task(void *pvParameters)
{
    while (1)
    {

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_STARE_SCAN,
                                               pdTRUE, pdFALSE,
                                               portMAX_DELAY);
        if (bits & WIFI_STARE_SCAN)
        {
            do_wifi_scan();
        }

    }
}

