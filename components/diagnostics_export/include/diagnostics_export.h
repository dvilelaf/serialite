#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_log.h"

typedef struct {
    uint64_t uptime_ms;
    int reset_reason;
    uint32_t heap_free;
    uint32_t heap_minimum;
    event_log_status_t event_log;
    bool ap_started;
    const char *ap_ip;
    uint32_t wifi_clients;
    bool usb_connected;
    uint64_t usb_bytes_received;
    uint64_t usb_bytes_sent;
    uint64_t bridge_bytes_from_usb;
    uint64_t bridge_bytes_to_usb;
    uint64_t bridge_dropped_from_usb;
    uint64_t bridge_dropped_to_usb;
    uint32_t bridge_subscribers;
    const event_log_entry_t *events;
    size_t event_count;
} diagnostics_export_snapshot_t;

int diagnostics_export_write_json(char *out, size_t out_size, const diagnostics_export_snapshot_t *snapshot);
int diagnostics_export_write_html_escaped(char *out, size_t out_size, const char *value);
