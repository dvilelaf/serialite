#include <stdio.h>
#include <stdlib.h>

#include "startup_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_ephemeral_credentials_require_working_local_display_before_ap(void)
{
    CHECK(startup_policy_after_ui(false, true) == STARTUP_POLICY_SKIP_AP);
    CHECK(startup_policy_after_ui(true, true) == STARTUP_POLICY_CONTINUE);
    CHECK(startup_policy_after_ui(false, false) == STARTUP_POLICY_CONTINUE);
}

static void test_web_failure_after_wifi_start_keeps_rescue_ap(void)
{
    CHECK(startup_policy_after_web(true, false) == STARTUP_POLICY_CONTINUE);
    CHECK(startup_policy_after_web(true, true) == STARTUP_POLICY_CONTINUE);
    CHECK(startup_policy_after_web(false, false) == STARTUP_POLICY_CONTINUE);
}

int main(void)
{
    test_ephemeral_credentials_require_working_local_display_before_ap();
    test_web_failure_after_wifi_start_keeps_rescue_ap();
    return 0;
}
