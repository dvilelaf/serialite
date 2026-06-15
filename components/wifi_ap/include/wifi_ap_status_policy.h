#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char ip_addr[16];
    uint8_t connected_clients;
    bool started;
} wifi_ap_status_policy_t;

wifi_ap_status_policy_t wifi_ap_status_reconcile(
    wifi_ap_status_policy_t cached,
    bool driver_ap_active,
    uint8_t driver_connected_clients);
