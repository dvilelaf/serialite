#include <stdio.h>
#include <stdlib.h>

#include "ap_exposure_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_idle_ap_stops_after_timeout(void)
{
    ap_exposure_policy_state_t state;
    ap_exposure_policy_init(&state);

    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 0, 1000) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 0, 1000 + AP_EXPOSURE_IDLE_TIMEOUT_MS - 1) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 0, 1000 + AP_EXPOSURE_IDLE_TIMEOUT_MS) == AP_EXPOSURE_STOP_AP);
}

static void test_any_client_keeps_ap_active_and_resets_idle_timer(void)
{
    ap_exposure_policy_state_t state;
    ap_exposure_policy_init(&state);

    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 0, 1000) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(ap_exposure_policy_evaluate(&state, true, 1, 0, 1000 + AP_EXPOSURE_IDLE_TIMEOUT_MS + 1) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 1, 1000 + (AP_EXPOSURE_IDLE_TIMEOUT_MS * 2)) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(ap_exposure_policy_evaluate(&state, true, 0, 0, 1000 + (AP_EXPOSURE_IDLE_TIMEOUT_MS * 2) + 1) == AP_EXPOSURE_KEEP_ACTIVE);
}

static void test_stopped_ap_does_not_track_idle(void)
{
    ap_exposure_policy_state_t state;
    ap_exposure_policy_init(&state);

    CHECK(ap_exposure_policy_evaluate(&state, false, 0, 0, 1000) == AP_EXPOSURE_KEEP_ACTIVE);
    CHECK(!state.idle_tracking);
}

int main(void)
{
    test_idle_ap_stops_after_timeout();
    test_any_client_keeps_ap_active_and_resets_idle_timer();
    test_stopped_ap_does_not_track_idle();
    return 0;
}
