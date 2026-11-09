#include "ap_exposure_policy.h"

#include <string.h>

void ap_exposure_policy_init(ap_exposure_policy_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

ap_exposure_policy_result_t ap_exposure_policy_evaluate(
    ap_exposure_policy_state_t *state,
    bool ap_started,
    uint32_t wifi_clients,
    uint32_t web_clients,
    uint64_t now_ms)
{
    if (state == NULL || !ap_started) {
        if (state != NULL) {
            state->idle_tracking = false;
            state->idle_since_ms = 0;
        }
        return AP_EXPOSURE_KEEP_ACTIVE;
    }

    if (wifi_clients > 0 || web_clients > 0) {
        state->idle_tracking = false;
        state->idle_since_ms = 0;
        return AP_EXPOSURE_KEEP_ACTIVE;
    }

    if (!state->idle_tracking) {
        state->idle_tracking = true;
        state->idle_since_ms = now_ms;
        return AP_EXPOSURE_KEEP_ACTIVE;
    }

    if (now_ms - state->idle_since_ms >= AP_EXPOSURE_IDLE_TIMEOUT_MS) {
        return AP_EXPOSURE_STOP_AP;
    }

    return AP_EXPOSURE_KEEP_ACTIVE;
}
