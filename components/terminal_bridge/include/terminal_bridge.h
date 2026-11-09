#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>

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

/*
 * Byte pipe between the serial console and its viewers. Output from the
 * server goes to a scrollback buffer and to every subscriber; input from
 * viewers is queued for the serial writer.
 */
esp_err_t terminal_bridge_start(void);
/* Subscribers run in the serial reader's task and must not block. */
esp_err_t terminal_bridge_register_output_callback(terminal_bridge_output_cb_t callback, void *ctx);
/* Called by the serial reader with bytes received from the server. */
size_t terminal_bridge_publish_usb_output(const uint8_t *data, size_t len);
/* Queues operator input for the server; returns how many bytes fit. */
size_t terminal_bridge_submit_input(const uint8_t *data, size_t len);
/* Called by the serial writer. */
size_t terminal_bridge_read_input_for_usb(uint8_t *data, size_t len, TickType_t timeout_ticks);
size_t terminal_bridge_snapshot_recent_output(uint8_t *data, size_t len);
terminal_bridge_status_t terminal_bridge_get_status(void);
