#include "wifi_time_sync.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "app_rtc/rtc.h"
#include "app_net/wifi_driver.h"
#include "app_net/wifi_credentials.h"

#define TAG "wifi_time"

#define NTP_SERVER "pool.ntp.org"
#define GEOIP_URL "http://ip-api.com/json?fields=status,offset"

/* Used only if the geo-IP lookup fails; US Eastern, DST-aware. */
#define FALLBACK_STD_OFFSET_SEC  (-5 * 3600)
#define FALLBACK_DST_OFFSET_SEC  (-4 * 3600)

#define WIFI_CONNECTED_BIT BIT0

static bool s_initialized;
static bool s_sync_in_progress;
static EventGroupHandle_t s_wifi_event_group;
static wifi_time_sync_cb_t s_cb;
static char s_http_resp[256];
static int s_http_resp_len;

/* Day of week (0=Sunday) via Sakamoto's algorithm. */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/* US DST: 2nd Sunday in March through 1st Sunday in November. */
static bool us_eastern_dst_active(int year, int month, int day)
{
    int second_sunday_mar = 1 + ((7 - day_of_week(year, 3, 1)) % 7) + 7;
    int first_sunday_nov = 1 + ((7 - day_of_week(year, 11, 1)) % 7);

    if (month > 3 && month < 11) {
        return true;
    }
    if (month == 3) {
        return day >= second_sunday_mar;
    }
    if (month == 11) {
        return day < first_sunday_nov;
    }
    return false;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int space = (int)sizeof(s_http_resp) - s_http_resp_len - 1;
        int copy_len = evt->data_len < space ? evt->data_len : space;
        if (copy_len > 0) {
            memcpy(s_http_resp + s_http_resp_len, evt->data, copy_len);
            s_http_resp_len += copy_len;
            s_http_resp[s_http_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

/* Best-effort geo-IP lookup for the current UTC offset (DST already applied). */
static bool fetch_utc_offset_seconds(int *out_offset)
{
    s_http_resp_len = 0;
    s_http_resp[0] = '\0';

    esp_http_client_config_t config = {
        .url = GEOIP_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Geo-IP request failed: err=%d status=%d", err, status);
        return false;
    }

    cJSON *root = cJSON_Parse(s_http_resp);
    if (!root) {
        ESP_LOGW(TAG, "Geo-IP response parse failed");
        return false;
    }

    cJSON *status_item = cJSON_GetObjectItem(root, "status");
    cJSON *offset_item = cJSON_GetObjectItem(root, "offset");
    bool ok = cJSON_IsString(status_item) && strcmp(status_item->valuestring, "success") == 0 &&
              cJSON_IsNumber(offset_item);
    if (ok) {
        *out_offset = offset_item->valueint;
    }
    cJSON_Delete(root);
    return ok;
}

/* Releases WiFi, fires the caller's callback, and deletes the calling task.
 * Common tail for every exit path out of wifi_time_sync_task(). */
static void finish_sync(wifi_time_sync_result_t result)
{
    wifi_driver_release();
    s_sync_in_progress = false;

    wifi_time_sync_cb_t cb = s_cb;
    s_cb = NULL;
    if (cb) {
        cb(result);
    }
    vTaskDelete(NULL);
}

static void wifi_time_sync_task(void *arg)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connect timed out, RTC left unchanged");
        finish_sync(WIFI_TIME_SYNC_ERR_WIFI);
        return;
    }
    ESP_LOGI(TAG, "WiFi connected");

    int utc_offset_seconds;
    bool have_offset = fetch_utc_offset_seconds(&utc_offset_seconds);

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    esp_netif_sntp_init(&sntp_config);
    esp_err_t sync_err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
    if (sync_err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP sync failed: %s", esp_err_to_name(sync_err));
        esp_netif_sntp_deinit();
        finish_sync(WIFI_TIME_SYNC_ERR_NTP);
        return;
    }

    time_t utc_now = time(NULL);

    if (!have_offset) {
        struct tm utc_tm;
        gmtime_r(&utc_now, &utc_tm);
        bool dst = us_eastern_dst_active(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);
        utc_offset_seconds = dst ? FALLBACK_DST_OFFSET_SEC : FALLBACK_STD_OFFSET_SEC;
        ESP_LOGW(TAG, "Geo-IP lookup failed, falling back to %s", dst ? "EDT (UTC-4)" : "EST (UTC-5)");
    } else {
        ESP_LOGI(TAG, "Geo-IP UTC offset: %d s", utc_offset_seconds);
    }

    time_t local_now = utc_now + utc_offset_seconds;
    struct tm local_tm;
    gmtime_r(&local_now, &local_tm);

    pcf85063a_datetime_t dt = {
        .year = local_tm.tm_year + 1900,
        .month = local_tm.tm_mon + 1,
        .day = local_tm.tm_mday,
        .dotw = local_tm.tm_wday,
        .hour = local_tm.tm_hour,
        .min = local_tm.tm_min,
        .sec = local_tm.tm_sec,
    };
    esp_err_t set_err = rtc_set_time(&dt);
    ESP_LOGI(TAG, "RTC set to %04d-%02d-%02d %02d:%02d:%02d (%s)",
             dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec,
             set_err == ESP_OK ? "ok" : "failed");

    esp_netif_sntp_deinit();
    finish_sync(set_err == ESP_OK ? WIFI_TIME_SYNC_OK : WIFI_TIME_SYNC_ERR_NTP);
}

static void ensure_initialized(void)
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;

    s_wifi_event_group = xEventGroupCreate();

    wifi_driver_init_once();

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
}

void wifi_time_sync_start(wifi_time_sync_cb_t cb)
{
    ensure_initialized();

    if (s_sync_in_progress) {
        ESP_LOGW(TAG, "Sync already in progress, ignoring request");
        return;
    }
    s_sync_in_progress = true;
    s_cb = cb;

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    wifi_driver_acquire();
    /* WIFI_EVENT_STA_START normally triggers the handler's connect call,
     * but the driver may already be started by another feature (e.g. the
     * WiFi scan screen), in which case that event already fired and won't
     * come again - so connect explicitly here too. */
    esp_wifi_connect();

    xTaskCreate(wifi_time_sync_task, "wifi_time_sync", 6144, NULL, 4, NULL);
}
