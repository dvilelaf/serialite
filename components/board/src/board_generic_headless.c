/* Any ESP32-S3 devkit: one active-low button, no display, no battery gauge. */

#include "board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"

static const char *TAG = "board_generic";

static const board_info_t s_info = {
    .name = "Generic ESP32-S3 (headless)",
    .has_display = false,
    .has_battery = false,
    .can_power_off = false,
    .buttons = {
        [BOARD_BUTTON_PRIMARY] = true,
        [BOARD_BUTTON_SECONDARY] = false,
    },
};

static bool s_initialized;

esp_err_t board_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_SERIALITE_BOARD_PRIMARY_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "button init failed");
    s_initialized = true;
    return ESP_OK;
}

const board_info_t *board_get_info(void)
{
    return &s_info;
}

bool board_button_pressed(board_button_t button)
{
    return s_initialized && button == BOARD_BUTTON_PRIMARY &&
           gpio_get_level(CONFIG_SERIALITE_BOARD_PRIMARY_BUTTON_GPIO) == 0;
}

esp_err_t board_battery_read(board_battery_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_power_off(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
