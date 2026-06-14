#include "power_status.h"

#include "driver/i2c.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

#define AXP2101_I2C_ADDR 0x34
#define AXP2101_STATUS_REG 0x00
#define AXP2101_CHARGE_STATUS_REG 0x01
#define AXP2101_BATTERY_PERCENT_REG 0xA4
#define AXP2101_READ_TIMEOUT_MS 20

#define BOARD_I2C_HOST I2C_NUM_0

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(
        BOARD_I2C_HOST,
        AXP2101_I2C_ADDR,
        &reg,
        sizeof(reg),
        value,
        sizeof(*value),
        pdMS_TO_TICKS(AXP2101_READ_TIMEOUT_MS));
}

esp_err_t power_status_read(power_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, "power_status", "invalid status pointer");

    *out_status = (power_status_t){
        .available = false,
        .percent = -1,
        .usb_connected = false,
        .charging = false,
    };

    uint8_t battery_percent = 0;
    esp_err_t ret = read_reg(AXP2101_BATTERY_PERCENT_REG, &battery_percent);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t power_status = 0;
    ret = read_reg(AXP2101_STATUS_REG, &power_status);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t charge_status = 0;
    ret = read_reg(AXP2101_CHARGE_STATUS_REG, &charge_status);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t charge_state = (charge_status >> 5) & 0x07;
    out_status->available = true;
    out_status->percent = battery_percent > 100 ? 100 : battery_percent;
    out_status->usb_connected = (power_status & 0x20) != 0;
    out_status->charging = charge_state >= 1 && charge_state <= 4;
    return ESP_OK;
}
