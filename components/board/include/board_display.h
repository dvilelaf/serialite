#pragma once

/* Display half of the board abstraction; only boards with has_display implement it. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

typedef struct {
    /* Native panel resolution, before any UI rotation. */
    uint16_t width;
    uint16_t height;
    /* Clockwise rotation in degrees (0, 90, 180, 270) that gives the UI a landscape layout. */
    uint16_t rotation_degrees;
    /* The panel controller needs flush areas aligned to even coordinates. */
    bool even_area_alignment;
} board_display_info_t;

const board_display_info_t *board_display_get_info(void);
/* Creates the panel; on_flush_done runs in ISR context when a transfer completes. */
esp_err_t board_display_create(
    esp_lcd_panel_io_color_trans_done_cb_t on_flush_done,
    void *user_ctx,
    esp_lcd_panel_handle_t *out_panel);
void board_display_set_power(bool on);
