#include "set_bat_msg.h"
#include "esp_log.h"

#include "bq27220.h"
#include "i2c_bus.h"

static const char *TAG = "set_bat_msg";


static const parameter_cedv_t default_cedv = {
    .full_charge_cap = 500,
    .design_cap = 500,
    .reserve_cap = 0,
    .near_full = 200,
    .self_discharge_rate = 20,
    .EDV0 = 3490,
    .EDV1 = 3511,
    .EDV2 = 3535,
    .EMF = 3670,
    .C0 = 115,
    .R0 = 968,
    .T0 = 4547,
    .R1 = 4764,
    .TC = 11,
    .C1 = 0,
    .DOD0 = 4147,
    .DOD10 = 4002,
    .DOD20 = 3969,
    .DOD30 = 3938,
    .DOD40 = 3880,
    .DOD50 = 3824,
    .DOD60 = 3794,
    .DOD70 = 3753,
    .DOD80 = 3677,
    .DOD90 = 3574,
    .DOD100 = 3490,
};

static const gauging_config_t default_config = {
    .CCT = 1,
    .CSYNC = 0,
    .EDV_CMP = 0,
    .SC = 1,
    .FIXED_EDV0 = 0,
    .FCC_LIM = 1,
    .FC_FOR_VDQ = 1,
    .IGNORE_SD = 1,
    .SME0 = 0,
};

static bq27220_handle_t bq27220 = NULL;
static i2c_bus_handle_t i2c_bus = NULL;

bq27220_handle_t bq27220_drv_init(void)
{

    i2c_bus = bsp_i2c_bus_get_handle();

    bq27220_config_t bq27220_cfg = {
        .i2c_bus = i2c_bus,
        .cfg = &default_config,
        .cedv = &default_cedv,
    };
    bq27220 = bq27220_create(&bq27220_cfg);

    if (!bq27220) {
        ESP_LOGE(TAG, "bq27220 create failed");
    }
    return bq27220;
}

bq27220_handle_t bq27220_drv_get_handle(void)
{
    return bq27220;
}
