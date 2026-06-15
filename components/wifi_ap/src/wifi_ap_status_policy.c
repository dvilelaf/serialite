#include "wifi_ap_status_policy.h"

wifi_ap_status_policy_t wifi_ap_status_reconcile(
    wifi_ap_status_policy_t cached,
    bool driver_ap_active,
    uint8_t driver_connected_clients)
{
    if (!driver_ap_active) {
        cached.started = false;
        cached.connected_clients = 0;
        return cached;
    }

    cached.started = true;
    cached.connected_clients = driver_connected_clients;
    return cached;
}
