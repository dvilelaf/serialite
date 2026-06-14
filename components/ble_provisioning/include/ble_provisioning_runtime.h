#pragma once

#include "ble_provisioning_policy.h"

#include <stdbool.h>

typedef enum {
    BLE_PROVISIONING_RUNTIME_STATE_IDLE = 0,
    BLE_PROVISIONING_RUNTIME_STATE_ADVERTISING,
} ble_provisioning_runtime_state_t;

typedef enum {
    BLE_PROVISIONING_RUNTIME_STARTED = 0,
    BLE_PROVISIONING_RUNTIME_ALREADY_RUNNING,
    BLE_PROVISIONING_RUNTIME_REJECTED,
    BLE_PROVISIONING_RUNTIME_RADIO_FAILED,
} ble_provisioning_runtime_result_t;

typedef bool (*ble_provisioning_runtime_start_radio_fn_t)(uint32_t advertising_window_seconds, void *ctx);
typedef void (*ble_provisioning_runtime_stop_radio_fn_t)(void *ctx);

typedef struct {
    ble_provisioning_policy_request_t policy;
    ble_provisioning_runtime_start_radio_fn_t start_radio;
    ble_provisioning_runtime_stop_radio_fn_t stop_radio;
    void *radio_ctx;
} ble_provisioning_runtime_config_t;

typedef struct {
    ble_provisioning_runtime_state_t state;
    ble_provisioning_runtime_stop_radio_fn_t stop_radio;
    void *radio_ctx;
    const char *last_reason;
} ble_provisioning_runtime_t;

void ble_provisioning_runtime_init(ble_provisioning_runtime_t *runtime);
ble_provisioning_runtime_result_t ble_provisioning_runtime_start(
    ble_provisioning_runtime_t *runtime,
    const ble_provisioning_runtime_config_t *config);
void ble_provisioning_runtime_stop(ble_provisioning_runtime_t *runtime);
ble_provisioning_runtime_state_t ble_provisioning_runtime_state(const ble_provisioning_runtime_t *runtime);
const char *ble_provisioning_runtime_last_reason(const ble_provisioning_runtime_t *runtime);
