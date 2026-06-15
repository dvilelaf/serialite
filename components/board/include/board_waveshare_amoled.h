#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include <stdint.h>

#define BOARD_LCD_H_RES 368
#define BOARD_LCD_V_RES 448
#define BOARD_LCD_BITS_PER_PIXEL 16

esp_err_t board_waveshare_amoled_init(void);

esp_err_t board_waveshare_amoled_new_display(const esp_lcd_panel_io_callbacks_t *callbacks,
                                             void *user_ctx,
                                             esp_lcd_panel_handle_t *out_panel,
                                             esp_lcd_panel_io_handle_t *out_io);

esp_err_t board_waveshare_amoled_new_touch(esp_lcd_touch_handle_t *out_touch);
bool board_waveshare_amoled_touch_signal_active(void);
bool board_waveshare_amoled_wake_button_active(void);
bool board_waveshare_amoled_security_button_active(void);

void board_waveshare_amoled_set_brightness(uint8_t brightness);
void board_waveshare_amoled_display_on(void);
void board_waveshare_amoled_display_off(void);
