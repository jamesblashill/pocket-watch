file(GLOB_RECURSE MAIN_SRCS ./*.cpp ./*.c)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
    REQUIRES "waveshare__pcf85063a" "waveshare__esp32_s3_touch_lcd_1_85B" "bsp_extra" "espressif__bq27220" "espressif__i2c_bus" "espressif__button" "lvgl__lvgl" "esp_wifi" "esp_event" "esp_netif" "esp_http_client" "json" "nvs_flash" "esp_pm" "esp_timer"
)