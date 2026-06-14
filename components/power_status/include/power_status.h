#pragma once

#include "esp_err.h"

#include <stdbool.h>

typedef struct {
    bool available;
    int percent;
    bool usb_connected;
    bool charging;
} power_status_t;

esp_err_t power_status_read(power_status_t *out_status);
