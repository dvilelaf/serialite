#include "ble_provisioning_policy.h"

#include <stddef.h>

bool ble_provisioning_policy_default_enabled(void)
{
    return false;
}

ble_provisioning_policy_result_t ble_provisioning_policy_can_enable(const ble_provisioning_policy_request_t *request)
{
    if (request == NULL || !request->requested) {
        return BLE_PROVISIONING_POLICY_REJECT_DISABLED;
    }
    if (!request->physical_presence) {
        return BLE_PROVISIONING_POLICY_REJECT_NO_PHYSICAL_PRESENCE;
    }
    if (!request->local_pairing_passed) {
        return BLE_PROVISIONING_POLICY_REJECT_NO_LOCAL_PAIRING;
    }
    if (!request->nvs_encrypted) {
        return BLE_PROVISIONING_POLICY_REJECT_UNENCRYPTED_NVS;
    }
    if (!request->offline_ap_flow_available) {
        return BLE_PROVISIONING_POLICY_REJECT_OFFLINE_FLOW_DISABLED;
    }
    if (request->advertising_window_seconds == 0U ||
        request->advertising_window_seconds > BLE_PROVISIONING_POLICY_ADV_WINDOW_MAX_SECONDS) {
        return BLE_PROVISIONING_POLICY_REJECT_WINDOW_TOO_LONG;
    }
    if (request->session_budget_seconds == 0U ||
        request->session_budget_seconds > BLE_PROVISIONING_POLICY_SESSION_MAX_SECONDS) {
        return BLE_PROVISIONING_POLICY_REJECT_SESSION_TOO_LONG;
    }
    return BLE_PROVISIONING_POLICY_ALLOW;
}

const char *ble_provisioning_policy_result_name(ble_provisioning_policy_result_t result)
{
    switch (result) {
        case BLE_PROVISIONING_POLICY_ALLOW:
            return "allow";
        case BLE_PROVISIONING_POLICY_REJECT_DISABLED:
            return "disabled";
        case BLE_PROVISIONING_POLICY_REJECT_NO_PHYSICAL_PRESENCE:
            return "no_physical_presence";
        case BLE_PROVISIONING_POLICY_REJECT_NO_LOCAL_PAIRING:
            return "no_local_pairing";
        case BLE_PROVISIONING_POLICY_REJECT_UNENCRYPTED_NVS:
            return "unencrypted_nvs";
        case BLE_PROVISIONING_POLICY_REJECT_OFFLINE_FLOW_DISABLED:
            return "offline_flow_disabled";
        case BLE_PROVISIONING_POLICY_REJECT_WINDOW_TOO_LONG:
            return "window_too_long";
        case BLE_PROVISIONING_POLICY_REJECT_SESSION_TOO_LONG:
            return "session_too_long";
        default:
            return "unknown";
    }
}
