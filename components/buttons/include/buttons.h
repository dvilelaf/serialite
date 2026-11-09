#pragma once

/*
 * Polls the board buttons and turns presses into actions. All callbacks run
 * in the buttons task and must not block for long.
 */

#include <stdint.h>

#include "esp_err.h"

#define BUTTONS_FACTORY_RESET_HOLD_MS 10000U
#define BUTTONS_POWER_OFF_HOLD_MS 3000U

typedef struct {
    /* Primary button went down: wake the screen or reveal secrets. */
    void (*on_primary_press)(void *ctx);
    /* Called on every poll while the primary button is held, and once with 0 on release. */
    void (*on_primary_hold)(uint32_t held_ms, void *ctx);
    /* Primary button held for BUTTONS_FACTORY_RESET_HOLD_MS. */
    void (*on_factory_reset)(void *ctx);
    /* Secondary button tapped: toggle the input lock. */
    void (*on_secondary_press)(void *ctx);
    /* Secondary button held for BUTTONS_POWER_OFF_HOLD_MS. */
    void (*on_secondary_hold)(void *ctx);
    void *ctx;
} buttons_config_t;

/* Requires board_init(). The config is copied. */
esp_err_t buttons_start(const buttons_config_t *config);
