#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BLE_PROVISIONING_POLICY_ADV_WINDOW_MAX_SECONDS 180U
#define BLE_PROVISIONING_POLICY_SESSION_MAX_SECONDS 600U

typedef enum {
    BLE_PROVISIONING_POLICY_REJECT_DISABLED = 0,
    BLE_PROVISIONING_POLICY_ALLOW,
    BLE_PROVISIONING_POLICY_REJECT_NO_PHYSICAL_PRESENCE,
    BLE_PROVISIONING_POLICY_REJECT_NO_LOCAL_PAIRING,
    BLE_PROVISIONING_POLICY_REJECT_UNENCRYPTED_NVS,
    BLE_PROVISIONING_POLICY_REJECT_OFFLINE_FLOW_DISABLED,
    BLE_PROVISIONING_POLICY_REJECT_WINDOW_TOO_LONG,
    BLE_PROVISIONING_POLICY_REJECT_SESSION_TOO_LONG,
} ble_provisioning_policy_result_t;

typedef struct {
    bool requested;
    bool physical_presence;
    bool local_pairing_passed;
    bool nvs_encrypted;
    bool offline_ap_flow_available;
    uint32_t advertising_window_seconds;
    uint32_t session_budget_seconds;
} ble_provisioning_policy_request_t;

bool ble_provisioning_policy_default_enabled(void);
ble_provisioning_policy_result_t ble_provisioning_policy_can_enable(const ble_provisioning_policy_request_t *request);
const char *ble_provisioning_policy_result_name(ble_provisioning_policy_result_t result);
