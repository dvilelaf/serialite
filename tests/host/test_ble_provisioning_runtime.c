#include "ble_provisioning_runtime.h"

#include <assert.h>
#include <string.h>

typedef struct {
    unsigned start_calls;
    unsigned stop_calls;
    uint32_t last_window_seconds;
} fake_radio_t;

static bool fake_start(uint32_t advertising_window_seconds, void *ctx)
{
    fake_radio_t *radio = (fake_radio_t *)ctx;
    radio->start_calls++;
    radio->last_window_seconds = advertising_window_seconds;
    return true;
}

static void fake_stop(void *ctx)
{
    fake_radio_t *radio = (fake_radio_t *)ctx;
    radio->stop_calls++;
}

static void rejected_policy_does_not_start_radio(void)
{
    fake_radio_t radio = {0};
    ble_provisioning_runtime_t runtime;
    ble_provisioning_runtime_init(&runtime);

    const ble_provisioning_runtime_config_t config = {
        .policy = {
            .requested = false,
            .physical_presence = true,
            .local_pairing_passed = true,
            .nvs_encrypted = true,
            .offline_ap_flow_available = true,
            .advertising_window_seconds = 120,
            .session_budget_seconds = 300,
        },
        .start_radio = fake_start,
        .stop_radio = fake_stop,
        .radio_ctx = &radio,
    };

    assert(ble_provisioning_runtime_start(&runtime, &config) == BLE_PROVISIONING_RUNTIME_REJECTED);
    assert(ble_provisioning_runtime_state(&runtime) == BLE_PROVISIONING_RUNTIME_STATE_IDLE);
    assert(strcmp(ble_provisioning_runtime_last_reason(&runtime), "disabled") == 0);
    assert(radio.start_calls == 0);
    assert(radio.stop_calls == 0);
}

static void allowed_policy_starts_radio_once(void)
{
    fake_radio_t radio = {0};
    ble_provisioning_runtime_t runtime;
    ble_provisioning_runtime_init(&runtime);

    const ble_provisioning_runtime_config_t config = {
        .policy = {
            .requested = true,
            .physical_presence = true,
            .local_pairing_passed = true,
            .nvs_encrypted = true,
            .offline_ap_flow_available = true,
            .advertising_window_seconds = 120,
            .session_budget_seconds = 300,
        },
        .start_radio = fake_start,
        .stop_radio = fake_stop,
        .radio_ctx = &radio,
    };

    assert(ble_provisioning_runtime_start(&runtime, &config) == BLE_PROVISIONING_RUNTIME_STARTED);
    assert(ble_provisioning_runtime_start(&runtime, &config) == BLE_PROVISIONING_RUNTIME_ALREADY_RUNNING);
    assert(ble_provisioning_runtime_state(&runtime) == BLE_PROVISIONING_RUNTIME_STATE_ADVERTISING);
    assert(strcmp(ble_provisioning_runtime_last_reason(&runtime), "allow") == 0);
    assert(radio.start_calls == 1);
    assert(radio.last_window_seconds == 120);
}

static void stop_is_idempotent(void)
{
    fake_radio_t radio = {0};
    ble_provisioning_runtime_t runtime;
    ble_provisioning_runtime_init(&runtime);

    const ble_provisioning_runtime_config_t config = {
        .policy = {
            .requested = true,
            .physical_presence = true,
            .local_pairing_passed = true,
            .nvs_encrypted = true,
            .offline_ap_flow_available = true,
            .advertising_window_seconds = 120,
            .session_budget_seconds = 300,
        },
        .start_radio = fake_start,
        .stop_radio = fake_stop,
        .radio_ctx = &radio,
    };

    assert(ble_provisioning_runtime_start(&runtime, &config) == BLE_PROVISIONING_RUNTIME_STARTED);
    ble_provisioning_runtime_stop(&runtime);
    ble_provisioning_runtime_stop(&runtime);
    assert(ble_provisioning_runtime_state(&runtime) == BLE_PROVISIONING_RUNTIME_STATE_IDLE);
    assert(radio.stop_calls == 1);
}

int main(void)
{
    rejected_policy_does_not_start_radio();
    allowed_policy_starts_radio_once();
    stop_is_idempotent();
    return 0;
}
