#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AP_EXPOSURE_IDLE_TIMEOUT_MS (10ULL * 60ULL * 1000ULL)

typedef struct {
    uint64_t idle_since_ms;
    bool idle_tracking;
} ap_exposure_policy_state_t;

typedef enum {
    AP_EXPOSURE_KEEP_ACTIVE = 0,
    AP_EXPOSURE_STOP_AP,
} ap_exposure_policy_result_t;

void ap_exposure_policy_init(ap_exposure_policy_state_t *state);
ap_exposure_policy_result_t ap_exposure_policy_evaluate(
    ap_exposure_policy_state_t *state,
    bool ap_started,
    uint32_t wifi_clients,
    uint32_t web_clients,
    uint64_t now_ms);
