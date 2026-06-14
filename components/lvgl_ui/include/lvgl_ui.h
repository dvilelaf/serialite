#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    const char *ssid;
    const char *password;
    const char *web_password;
    const char *pairing_code;
    const char *ip_addr;
    bool usb_connected;
} lvgl_ui_boot_status_t;

esp_err_t lvgl_ui_start(const lvgl_ui_boot_status_t *status);
esp_err_t lvgl_ui_update_credentials(const lvgl_ui_boot_status_t *status, bool reveal);
esp_err_t lvgl_ui_set_pairing_status(const char *status_text);
