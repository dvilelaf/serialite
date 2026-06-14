#include "ble_provisioning_runtime.h"

#include <stddef.h>

void ble_provisioning_runtime_init(ble_provisioning_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    runtime->state = BLE_PROVISIONING_RUNTIME_STATE_IDLE;
    runtime->stop_radio = NULL;
    runtime->radio_ctx = NULL;
    runtime->last_reason = ble_provisioning_policy_result_name(BLE_PROVISIONING_POLICY_REJECT_DISABLED);
}

ble_provisioning_runtime_result_t ble_provisioning_runtime_start(
    ble_provisioning_runtime_t *runtime,
    const ble_provisioning_runtime_config_t *config)
{
    if (runtime == NULL || config == NULL || config->start_radio == NULL) {
        return BLE_PROVISIONING_RUNTIME_REJECTED;
    }
    if (runtime->state == BLE_PROVISIONING_RUNTIME_STATE_ADVERTISING) {
        return BLE_PROVISIONING_RUNTIME_ALREADY_RUNNING;
    }

    const ble_provisioning_policy_result_t policy = ble_provisioning_policy_can_enable(&config->policy);
    runtime->last_reason = ble_provisioning_policy_result_name(policy);
    if (policy != BLE_PROVISIONING_POLICY_ALLOW) {
        return BLE_PROVISIONING_RUNTIME_REJECTED;
    }

    if (!config->start_radio(config->policy.advertising_window_seconds, config->radio_ctx)) {
        runtime->last_reason = "radio_failed";
        return BLE_PROVISIONING_RUNTIME_RADIO_FAILED;
    }

    runtime->state = BLE_PROVISIONING_RUNTIME_STATE_ADVERTISING;
    runtime->stop_radio = config->stop_radio;
    runtime->radio_ctx = config->radio_ctx;
    return BLE_PROVISIONING_RUNTIME_STARTED;
}

void ble_provisioning_runtime_stop(ble_provisioning_runtime_t *runtime)
{
    if (runtime == NULL || runtime->state != BLE_PROVISIONING_RUNTIME_STATE_ADVERTISING) {
        return;
    }
    if (runtime->stop_radio != NULL) {
        runtime->stop_radio(runtime->radio_ctx);
    }
    runtime->state = BLE_PROVISIONING_RUNTIME_STATE_IDLE;
    runtime->stop_radio = NULL;
    runtime->radio_ctx = NULL;
}

ble_provisioning_runtime_state_t ble_provisioning_runtime_state(const ble_provisioning_runtime_t *runtime)
{
    return runtime == NULL ? BLE_PROVISIONING_RUNTIME_STATE_IDLE : runtime->state;
}

const char *ble_provisioning_runtime_last_reason(const ble_provisioning_runtime_t *runtime)
{
    if (runtime == NULL || runtime->last_reason == NULL) {
        return "unknown";
    }
    return runtime->last_reason;
}
