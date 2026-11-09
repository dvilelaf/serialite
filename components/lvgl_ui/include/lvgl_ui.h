#pragma once

/*
 * Local status screen. Only the UI task touches LVGL; every public function
 * here copies its arguments and hands them to that task, so they can be
 * called from any task.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui_status_format.h"

typedef struct {
    const char *ssid;
    /* Human-readable passphrase; shown only while revealed. */
    const char *password;
    /* Host or IP shown as the address to open. */
    const char *web_host;
    /* Certificate SHA-256 in AA:BB:.. form, or NULL when HTTPS is off. */
    const char *https_fingerprint;
} lvgl_ui_credentials_t;

/* Fills the live status; called from the UI task every couple of seconds. */
typedef void (*lvgl_ui_status_fn_t)(ui_status_format_input_t *out, void *ctx);

typedef struct {
    lvgl_ui_credentials_t credentials;
    lvgl_ui_status_fn_t status_fn;
    void *status_ctx;
} lvgl_ui_config_t;

/* ESP_ERR_NOT_SUPPORTED on boards without a display. */
esp_err_t lvgl_ui_start(const lvgl_ui_config_t *config);
bool lvgl_ui_is_running(void);
/* Replaces the shown credentials; reveal shows them right away. */
esp_err_t lvgl_ui_set_credentials(const lvgl_ui_credentials_t *credentials, bool reveal);
/* Primary button went down: wake the screen, or reveal / page through secrets. */
void lvgl_ui_notify_primary_press(void);
/* Primary button hold time, for the factory reset countdown; 0 on release. */
void lvgl_ui_notify_primary_hold(uint32_t held_ms, uint32_t reset_after_ms);
