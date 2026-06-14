#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UI_STATUS_LINE_MAX 64

typedef struct {
    bool ap_started;
    uint32_t wifi_clients;
    uint32_t web_clients;
    bool usb_connected;
    uint64_t usb_rx_bytes;
    uint64_t usb_tx_bytes;
    uint64_t bridge_drops;
} ui_status_format_input_t;

typedef struct {
    char usb_line[UI_STATUS_LINE_MAX];
    char client_line[UI_STATUS_LINE_MAX];
    char error_line[UI_STATUS_LINE_MAX];
} ui_status_format_output_t;

bool ui_status_format(const ui_status_format_input_t *input, ui_status_format_output_t *output);
