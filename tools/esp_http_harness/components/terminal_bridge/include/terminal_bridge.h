#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>
typedef enum { TERMINAL_BRIDGE_SOURCE_WEB = 1, TERMINAL_BRIDGE_SOURCE_LVGL = 2 } terminal_bridge_source_t;
typedef void (*terminal_bridge_output_cb_t)(const uint8_t *data, size_t len, void *ctx);
typedef struct {
    uint64_t bytes_from_usb;
    uint64_t bytes_to_usb;
    uint64_t dropped_from_usb;
    uint64_t dropped_to_usb;
    uint64_t scrollback_dropped_oldest;
    uint32_t subscriber_count;
    uint32_t scrollback_retained;
    uint32_t scrollback_capacity;
} terminal_bridge_status_t;
esp_err_t terminal_bridge_start(void);
esp_err_t terminal_bridge_register_output_callback(terminal_bridge_output_cb_t callback, void *ctx);
size_t terminal_bridge_publish_usb_output(const uint8_t *data, size_t len);
size_t terminal_bridge_submit_input(terminal_bridge_source_t source, const uint8_t *data, size_t len);
size_t terminal_bridge_read_input_for_usb(uint8_t *data, size_t len, TickType_t timeout_ticks);
size_t terminal_bridge_snapshot_recent_output(uint8_t *data, size_t len);
terminal_bridge_status_t terminal_bridge_get_status(void);
