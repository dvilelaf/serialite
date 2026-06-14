#include "ble_provisioning_policy.h"

#include <assert.h>
#include <string.h>

static void disabled_by_default(void)
{
    assert(!ble_provisioning_policy_default_enabled());
}

static void enable_requires_local_presence_and_explicit_security_gates(void)
{
    assert(ble_provisioning_policy_can_enable(NULL) == BLE_PROVISIONING_POLICY_REJECT_DISABLED);

    const ble_provisioning_policy_request_t valid = {
        .requested = true,
        .physical_presence = true,
        .local_pairing_passed = true,
        .nvs_encrypted = true,
        .offline_ap_flow_available = true,
        .advertising_window_seconds = 120,
        .session_budget_seconds = 300,
    };

    assert(ble_provisioning_policy_can_enable(&valid) == BLE_PROVISIONING_POLICY_ALLOW);

    ble_provisioning_policy_request_t req = valid;
    req.requested = false;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_DISABLED);

    req = valid;
    req.physical_presence = false;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_NO_PHYSICAL_PRESENCE);

    req = valid;
    req.local_pairing_passed = false;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_NO_LOCAL_PAIRING);

    req = valid;
    req.nvs_encrypted = false;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_UNENCRYPTED_NVS);

    req = valid;
    req.offline_ap_flow_available = false;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_OFFLINE_FLOW_DISABLED);
}

static void enable_rejects_unbounded_radio_exposure(void)
{
    ble_provisioning_policy_request_t req = {
        .requested = true,
        .physical_presence = true,
        .local_pairing_passed = true,
        .nvs_encrypted = true,
        .offline_ap_flow_available = true,
        .advertising_window_seconds = BLE_PROVISIONING_POLICY_ADV_WINDOW_MAX_SECONDS + 1U,
        .session_budget_seconds = 300,
    };
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_WINDOW_TOO_LONG);

    req.advertising_window_seconds = 0;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_WINDOW_TOO_LONG);

    req.advertising_window_seconds = 120;
    req.session_budget_seconds = BLE_PROVISIONING_POLICY_SESSION_MAX_SECONDS + 1U;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_SESSION_TOO_LONG);

    req.session_budget_seconds = 0;
    assert(ble_provisioning_policy_can_enable(&req) == BLE_PROVISIONING_POLICY_REJECT_SESSION_TOO_LONG);
}

static void result_names_are_stable_for_logs(void)
{
    assert(strcmp(ble_provisioning_policy_result_name(BLE_PROVISIONING_POLICY_ALLOW), "allow") == 0);
    assert(strcmp(ble_provisioning_policy_result_name(BLE_PROVISIONING_POLICY_REJECT_NO_LOCAL_PAIRING), "no_local_pairing") == 0);
    assert(strcmp(ble_provisioning_policy_result_name((ble_provisioning_policy_result_t)999), "unknown") == 0);
}

int main(void)
{
    disabled_by_default();
    enable_requires_local_presence_and_explicit_security_gates();
    enable_rejects_unbounded_radio_exposure();
    result_names_are_stable_for_logs();
    return 0;
}
