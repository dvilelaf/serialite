#include "board_waveshare_amoled.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_amoled";

#define BOARD_I2C_HOST I2C_NUM_0
#define BOARD_LCD_HOST SPI2_HOST

#define BOARD_PIN_LCD_CS GPIO_NUM_12
#define BOARD_PIN_LCD_PCLK GPIO_NUM_11
#define BOARD_PIN_LCD_DATA0 GPIO_NUM_4
#define BOARD_PIN_LCD_DATA1 GPIO_NUM_5
#define BOARD_PIN_LCD_DATA2 GPIO_NUM_6
#define BOARD_PIN_LCD_DATA3 GPIO_NUM_7
#define BOARD_PIN_LCD_RST (-1)

#define BOARD_PIN_TOUCH_SCL GPIO_NUM_14
#define BOARD_PIN_TOUCH_SDA GPIO_NUM_15
#define BOARD_PIN_TOUCH_INT GPIO_NUM_21
#define BOARD_PIN_WAKE_BUTTON GPIO_NUM_0
#define BOARD_PIN_POWER_BUTTON IO_EXPANDER_PIN_NUM_4

static esp_io_expander_handle_t s_io_expander;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_i2c_ready;
static bool s_spi_ready;
static bool s_button_ready;
static bool s_power_button_ready;

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

static esp_err_t init_i2c(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_PIN_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BOARD_PIN_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 200 * 1000,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_HOST, &config), TAG, "i2c param config failed");
    esp_err_t err = i2c_driver_install(BOARD_I2C_HOST, config.mode, 0, 0, 0);
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "i2c install failed");
    }

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca9554(BOARD_I2C_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io_expander),
        TAG,
        "io expander init failed");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(
            s_io_expander,
            IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2,
            IO_EXPANDER_OUTPUT),
        TAG,
        "io expander direction failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_0, 0), TAG, "p0 low failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_1, 0), TAG, "p1 low failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_2, 0), TAG, "p2 low failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_io_expander, BOARD_PIN_POWER_BUTTON, IO_EXPANDER_INPUT), TAG, "power button direction failed");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_0, 1), TAG, "p0 high failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_1, 1), TAG, "p1 high failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_2, 1), TAG, "p2 high failed");

    s_i2c_ready = true;
    s_power_button_ready = true;
    return ESP_OK;
}

static esp_err_t init_spi(void)
{
    if (s_spi_ready) {
        return ESP_OK;
    }

    const spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        BOARD_PIN_LCD_PCLK,
        BOARD_PIN_LCD_DATA0,
        BOARD_PIN_LCD_DATA1,
        BOARD_PIN_LCD_DATA2,
        BOARD_PIN_LCD_DATA3,
        BOARD_LCD_H_RES * BOARD_LCD_V_RES * BOARD_LCD_BITS_PER_PIXEL / 8);

    esp_err_t err = spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "spi bus init failed");
    }

    s_spi_ready = true;
    return ESP_OK;
}

static esp_err_t init_wake_button(void)
{
    if (s_button_ready) {
        return ESP_OK;
    }

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << BOARD_PIN_WAKE_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button_config), TAG, "wake button config failed");

    s_button_ready = true;
    return ESP_OK;
}

esp_err_t board_waveshare_amoled_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c init failed");
    ESP_RETURN_ON_ERROR(init_spi(), TAG, "spi init failed");
    ESP_RETURN_ON_ERROR(init_wake_button(), TAG, "wake button init failed");
    return ESP_OK;
}

esp_err_t board_waveshare_amoled_new_display(const esp_lcd_panel_io_callbacks_t *callbacks,
                                             void *user_ctx,
                                             esp_lcd_panel_handle_t *out_panel,
                                             esp_lcd_panel_io_handle_t *out_io)
{
    ESP_RETURN_ON_FALSE(callbacks != NULL && out_panel != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid display args");
    ESP_RETURN_ON_ERROR(board_waveshare_amoled_init(), TAG, "board init failed");

    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(BOARD_PIN_LCD_CS, callbacks->on_color_trans_done, user_ctx);

    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_config, &io),
        TAG,
        "panel io create failed");

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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io, &panel_config, &panel), TAG, "panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on failed");

    s_panel_io = io;
    s_panel = panel;
    *out_panel = panel;
    if (out_io != NULL) {
        *out_io = io;
    }

    return ESP_OK;
}

esp_err_t board_waveshare_amoled_new_touch(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_FALSE(out_touch != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid touch args");
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c init failed");

    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    touch_io_config.scl_speed_hz = 0;  // Legacy i2c_driver path uses the bus speed configured in init_i2c().
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)BOARD_I2C_HOST, &touch_io_config, &touch_io),
        TAG,
        "touch io create failed");

    const esp_lcd_touch_config_t touch_config = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = BOARD_PIN_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, out_touch), TAG, "touch create failed");
    gpio_pullup_en(BOARD_PIN_TOUCH_INT);
    gpio_pulldown_dis(BOARD_PIN_TOUCH_INT);
    return ESP_OK;
}

bool board_waveshare_amoled_touch_signal_active(void)
{
    return gpio_get_level(BOARD_PIN_TOUCH_INT) == 0;
}

bool board_waveshare_amoled_wake_button_active(void)
{
    return s_button_ready && gpio_get_level(BOARD_PIN_WAKE_BUTTON) == 0;
}

bool board_waveshare_amoled_security_button_active(void)
{
    uint32_t levels = 0;
    if (!s_power_button_ready || s_io_expander == NULL ||
        esp_io_expander_get_level(s_io_expander, BOARD_PIN_POWER_BUTTON, &levels) != ESP_OK) {
        return false;
    }
    return (levels & BOARD_PIN_POWER_BUTTON) != 0;
}

void board_waveshare_amoled_set_brightness(uint8_t brightness)
{
    if (s_panel_io != NULL) {
        esp_lcd_panel_io_tx_param(s_panel_io, 0x51, &brightness, sizeof(brightness));
    }
}

void board_waveshare_amoled_display_on(void)
{
    if (s_panel != NULL) {
        esp_lcd_panel_disp_on_off(s_panel, true);
    }
    board_waveshare_amoled_set_brightness(0xFF);
    if (s_io_expander != NULL) {
        esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_2, 1);
    }
}

void board_waveshare_amoled_display_off(void)
{
    if (s_io_expander != NULL) {
        esp_io_expander_set_level(s_io_expander, IO_EXPANDER_PIN_NUM_2, 0);
    }
    board_waveshare_amoled_set_brightness(0x00);
    if (s_panel != NULL) {
        esp_lcd_panel_disp_on_off(s_panel, false);
    }
}
