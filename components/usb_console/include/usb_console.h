#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool connected;
    uint64_t bytes_received;
    uint64_t bytes_sent;
} usb_console_status_t;

esp_err_t usb_console_start(void);
usb_console_status_t usb_console_get_status(void);
size_t usb_console_write(const uint8_t *data, size_t len);
