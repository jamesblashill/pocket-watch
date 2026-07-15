#include "sys_msg_ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "bsp_board_extra.h"
#include "pcf85063a.h"
#include "submenu_ui/set_wifi_service.h"
#include "submenu_ui/set_bat_msg.h"

static pcf85063a_dev_t s_rtc_dev;

esp_err_t msg_driver_init(void)
{
    esp_err_t ret = ESP_OK;
    bq27220_drv_init();

    i2c_master_bus_handle_t i2c_bus;
    i2c_master_get_bus_handle(BSP_I2C_NUM,&i2c_bus);

    pcf85063a_init(&s_rtc_dev, i2c_bus, PCF85063A_ADDRESS);

    wifi_init_sta();
    return ret;
}
