/* Waveshare ESP32-S3-Touch-AMOLED-1.8: SH8601 QSPI AMOLED, TCA9554 expander, AXP2101 PMIC. */

#include "board.h"
#include "board_display.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_amoled";

#define BOARD_I2C_HOST I2C_NUM_0
#define BOARD_I2C_SPEED_HZ (200 * 1000)
#define BOARD_PIN_I2C_SCL GPIO_NUM_14
#define BOARD_PIN_I2C_SDA GPIO_NUM_15

#define BOARD_LCD_HOST SPI2_HOST
#define BOARD_LCD_H_RES 368
#define BOARD_LCD_V_RES 448
#define BOARD_LCD_BITS_PER_PIXEL 16
#define BOARD_PIN_LCD_CS GPIO_NUM_12
#define BOARD_PIN_LCD_PCLK GPIO_NUM_11
#define BOARD_PIN_LCD_DATA0 GPIO_NUM_4
#define BOARD_PIN_LCD_DATA1 GPIO_NUM_5
#define BOARD_PIN_LCD_DATA2 GPIO_NUM_6
#define BOARD_PIN_LCD_DATA3 GPIO_NUM_7
#define BOARD_PIN_LCD_RST (-1)
#define SH8601_CMD_BRIGHTNESS 0x51

#define BOARD_PIN_BOOT_BUTTON GPIO_NUM_0
/* Expander pins 0-2 gate the display and touch supplies; pin 2 also drives the AMOLED enable. */
#define EXPANDER_POWER_PINS (IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2)
#define EXPANDER_DISPLAY_ENABLE_PIN IO_EXPANDER_PIN_NUM_2
#define EXPANDER_PWR_BUTTON_PIN IO_EXPANDER_PIN_NUM_4
#define EXPANDER_POWER_SETTLE_MS 200

#define AXP2101_I2C_ADDR 0x34
#define AXP2101_REG_STATUS 0x00
#define AXP2101_REG_CHARGE_STATUS 0x01
#define AXP2101_REG_POWER_OFF 0x10
#define AXP2101_REG_BATTERY_PERCENT 0xA4
#define AXP2101_STATUS_VBUS_GOOD 0x20
#define AXP2101_POWER_OFF_BIT 0x01
#define AXP2101_TIMEOUT_MS 20

static const board_info_t s_info = {
    .name = "Waveshare ESP32-S3-Touch-AMOLED-1.8",
    .has_display = true,
    .has_battery = true,
    .can_power_off = true,
    .buttons = {
        [BOARD_BUTTON_PRIMARY] = true,
        [BOARD_BUTTON_SECONDARY] = true,
    },
};

static const board_display_info_t s_display_info = {
    .width = BOARD_LCD_H_RES,
    .height = BOARD_LCD_V_RES,
    .rotation_degrees = 270,
    .even_area_alignment = true,
};

static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_io_expander_handle_t s_io_expander;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_initialized;

static esp_err_t init_i2c(void)
{
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_PIN_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BOARD_PIN_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_HOST, &config), TAG, "i2c param config failed");
    const esp_err_t err = i2c_driver_install(BOARD_I2C_HOST, config.mode, 0, 0, 0);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static esp_err_t init_io_expander(void)
{
    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca9554(BOARD_I2C_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io_expander),
        TAG,
        "io expander init failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, EXPANDER_POWER_PINS, IO_EXPANDER_OUTPUT), TAG, "expander output dir failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, EXPANDER_POWER_PINS, 0), TAG, "expander power off failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, EXPANDER_PWR_BUTTON_PIN, IO_EXPANDER_INPUT), TAG, "PWR button dir failed");
    vTaskDelay(pdMS_TO_TICKS(EXPANDER_POWER_SETTLE_MS));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, EXPANDER_POWER_PINS, 1), TAG, "expander power on failed");
    return ESP_OK;
}

static esp_err_t init_boot_button(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t board_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(init_boot_button(), TAG, "BOOT button init failed");
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c init failed");
    ESP_RETURN_ON_ERROR(init_io_expander(), TAG, "io expander init failed");
    s_initialized = true;
    return ESP_OK;
}

const board_info_t *board_get_info(void)
{
    return &s_info;
}

bool board_button_pressed(board_button_t button)
{
    if (!s_initialized) {
        return false;
    }
    switch (button) {
        case BOARD_BUTTON_PRIMARY:
            return gpio_get_level(BOARD_PIN_BOOT_BUTTON) == 0;
        case BOARD_BUTTON_SECONDARY: {
            uint32_t levels = 0;
            return esp_io_expander_get_level(s_io_expander, EXPANDER_PWR_BUTTON_PIN, &levels) == ESP_OK &&
                   (levels & EXPANDER_PWR_BUTTON_PIN) != 0;
        }
        default:
            return false;
    }
}

static esp_err_t axp2101_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(
        BOARD_I2C_HOST, AXP2101_I2C_ADDR, &reg, sizeof(reg), value, sizeof(*value), pdMS_TO_TICKS(AXP2101_TIMEOUT_MS));
}

esp_err_t board_battery_read(board_battery_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "missing battery output");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "board not initialized");

    uint8_t percent = 0;
    uint8_t status = 0;
    uint8_t charge_status = 0;
    ESP_RETURN_ON_ERROR(axp2101_read(AXP2101_REG_BATTERY_PERCENT, &percent), TAG, "battery percent read failed");
    ESP_RETURN_ON_ERROR(axp2101_read(AXP2101_REG_STATUS, &status), TAG, "power status read failed");
    ESP_RETURN_ON_ERROR(axp2101_read(AXP2101_REG_CHARGE_STATUS, &charge_status), TAG, "charge status read failed");

    const uint8_t charge_state = (charge_status >> 5) & 0x07;
    *out = (board_battery_t){
        .percent = percent > 100 ? 100 : percent,
        .external_power = (status & AXP2101_STATUS_VBUS_GOOD) != 0,
        .charging = charge_state >= 1 && charge_state <= 4,
    };
    return ESP_OK;
}

esp_err_t board_power_off(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "board not initialized");
    const uint8_t command[] = {AXP2101_REG_POWER_OFF, AXP2101_POWER_OFF_BIT};
    return i2c_master_write_to_device(
        BOARD_I2C_HOST, AXP2101_I2C_ADDR, command, sizeof(command), pdMS_TO_TICKS(AXP2101_TIMEOUT_MS));
}

const board_display_info_t *board_display_get_info(void)
{
    return &s_display_info;
}

esp_err_t board_display_create(
    esp_lcd_panel_io_color_trans_done_cb_t on_flush_done,
    void *user_ctx,
    esp_lcd_panel_handle_t *out_panel)
{
    ESP_RETURN_ON_FALSE(out_panel != NULL, ESP_ERR_INVALID_ARG, TAG, "missing panel output");
    ESP_RETURN_ON_ERROR(board_init(), TAG, "board init failed");

    const spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        BOARD_PIN_LCD_PCLK,
        BOARD_PIN_LCD_DATA0,
        BOARD_PIN_LCD_DATA1,
        BOARD_PIN_LCD_DATA2,
        BOARD_PIN_LCD_DATA3,
        BOARD_LCD_H_RES * BOARD_LCD_V_RES * BOARD_LCD_BITS_PER_PIXEL / 8);
    const esp_err_t bus_err = spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    ESP_RETURN_ON_FALSE(bus_err == ESP_OK || bus_err == ESP_ERR_INVALID_STATE, bus_err, TAG, "spi bus init failed");

    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(BOARD_PIN_LCD_CS, on_flush_done, user_ctx);
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_config, &io), TAG, "panel io create failed");

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel = NULL;
    esp_err_t err = esp_lcd_new_panel_sh8601(io, &panel_config, &panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel bring-up failed: %s", esp_err_to_name(err));
        if (panel != NULL) {
            esp_lcd_panel_del(panel);
        }
        esp_lcd_panel_io_del(io);
        return err;
    }

    s_panel_io = io;
    s_panel = panel;
    *out_panel = panel;
    return ESP_OK;
}

static void set_brightness(uint8_t brightness)
{
    if (s_panel_io != NULL &&
        esp_lcd_panel_io_tx_param(s_panel_io, SH8601_CMD_BRIGHTNESS, &brightness, sizeof(brightness)) != ESP_OK) {
        ESP_LOGW(TAG, "brightness update failed");
    }
}

void board_display_set_power(bool on)
{
    if (on) {
        if (s_panel != NULL && esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) {
            ESP_LOGW(TAG, "panel on failed");
        }
        set_brightness(0xFF);
        if (s_io_expander != NULL && esp_io_expander_set_level(s_io_expander, EXPANDER_DISPLAY_ENABLE_PIN, 1) != ESP_OK) {
            ESP_LOGW(TAG, "display enable failed");
        }
        return;
    }

    if (s_io_expander != NULL && esp_io_expander_set_level(s_io_expander, EXPANDER_DISPLAY_ENABLE_PIN, 0) != ESP_OK) {
        ESP_LOGW(TAG, "display disable failed");
    }
    set_brightness(0x00);
    if (s_panel != NULL && esp_lcd_panel_disp_on_off(s_panel, false) != ESP_OK) {
        ESP_LOGW(TAG, "panel off failed");
    }
}
