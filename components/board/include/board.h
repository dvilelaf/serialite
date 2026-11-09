#pragma once

/*
 * Board abstraction. Application code talks to buttons, battery and power
 * through this header only; each supported board provides one implementation
 * selected with CONFIG_SERIALITE_BOARD_*.
 */

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    /* Wakes the screen, reveals secrets and, held long, factory resets. BOOT on the Waveshare board. */
    BOARD_BUTTON_PRIMARY = 0,
    /* Toggles the input lock and, held long, powers off. PWR on the Waveshare board. */
    BOARD_BUTTON_SECONDARY,
    BOARD_BUTTON_COUNT,
} board_button_t;

typedef struct {
    const char *name;
    bool has_display;
    bool has_battery;
    bool can_power_off;
    bool buttons[BOARD_BUTTON_COUNT];
} board_info_t;

typedef struct {
    int percent;
    bool external_power;
    bool charging;
} board_battery_t;

/* Brings up buses, IO expanders and buttons. Safe to call more than once. */
esp_err_t board_init(void);
const board_info_t *board_get_info(void);
/* Debouncing is left to the caller. Returns false for buttons the board lacks. */
bool board_button_pressed(board_button_t button);
/* ESP_ERR_NOT_SUPPORTED on boards without a battery gauge. */
esp_err_t board_battery_read(board_battery_t *out);
/* ESP_ERR_NOT_SUPPORTED on boards that cannot cut their own power. */
esp_err_t board_power_off(void);
