#include "power_control.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

#define AXP2101_I2C_ADDR 0x34
#define AXP2101_POWER_OFF_REG 0x10
#define AXP2101_POWER_OFF_BIT 0x01
#define AXP2101_WRITE_TIMEOUT_MS 20

#define BOARD_I2C_HOST I2C_NUM_0

esp_err_t power_control_power_off(void)
{
    const uint8_t command[] = {
        AXP2101_POWER_OFF_REG,
        AXP2101_POWER_OFF_BIT,
    };
    return i2c_master_write_to_device(
        BOARD_I2C_HOST,
        AXP2101_I2C_ADDR,
        command,
        sizeof(command),
        pdMS_TO_TICKS(AXP2101_WRITE_TIMEOUT_MS));
}
