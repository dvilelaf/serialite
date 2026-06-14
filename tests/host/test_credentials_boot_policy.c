#include <stdio.h>
#include <stdlib.h>

#include "credentials.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_visible_runtime_password_wins_over_persisted_hash(void)
{
    credentials_web_auth_boot_decision_t decision = {0};

    CHECK(credentials_web_auth_boot_decide(
              &(credentials_web_auth_boot_input_t){
                  .persisted_hash_configured = true,
                  .rtc_password_available = false,
              },
              &decision));

    CHECK(decision.generate_runtime_password);
    CHECK(!decision.use_rtc_password);
    CHECK(!decision.use_persisted_hash);
}

static void test_rtc_visible_password_is_reused_after_reboot(void)
{
    credentials_web_auth_boot_decision_t decision = {0};

    CHECK(credentials_web_auth_boot_decide(
              &(credentials_web_auth_boot_input_t){
                  .persisted_hash_configured = true,
                  .rtc_password_available = true,
              },
              &decision));

    CHECK(!decision.generate_runtime_password);
    CHECK(decision.use_rtc_password);
    CHECK(!decision.use_persisted_hash);
}

int main(void)
{
    test_visible_runtime_password_wins_over_persisted_hash();
    test_rtc_visible_password_is_reused_after_reboot();
    return 0;
}
