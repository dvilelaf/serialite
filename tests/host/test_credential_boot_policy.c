#include <stdio.h>
#include <stdlib.h>

#include "credential_boot_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static credential_source_t pick(bool stored, bool cache, bool warm)
{
    const credential_boot_facts_t facts = {.stored_valid = stored, .rtc_cache_valid = cache, .warm_reset = warm};
    return credential_boot_policy_select(&facts);
}

static void test_stored_credentials_win(void)
{
    CHECK(pick(true, false, false) == CREDENTIAL_SOURCE_STORED);
    CHECK(pick(true, true, true) == CREDENTIAL_SOURCE_STORED);
}

static void test_cache_only_survives_warm_resets(void)
{
    CHECK(pick(false, true, true) == CREDENTIAL_SOURCE_RTC_CACHE);
    CHECK(pick(false, true, false) == CREDENTIAL_SOURCE_GENERATE);
    CHECK(pick(false, false, true) == CREDENTIAL_SOURCE_GENERATE);
    CHECK(credential_boot_policy_select(NULL) == CREDENTIAL_SOURCE_GENERATE);
}

static void test_ephemeral_credentials_need_a_way_to_be_read(void)
{
    CHECK(credential_boot_policy_can_start_ap(CREDENTIAL_SOURCE_STORED, false, false));
    CHECK(credential_boot_policy_can_start_ap(CREDENTIAL_SOURCE_GENERATE, true, false));
    CHECK(credential_boot_policy_can_start_ap(CREDENTIAL_SOURCE_RTC_CACHE, false, true));
    CHECK(!credential_boot_policy_can_start_ap(CREDENTIAL_SOURCE_GENERATE, false, false));
    CHECK(!credential_boot_policy_can_start_ap(CREDENTIAL_SOURCE_RTC_CACHE, false, false));
}

int main(void)
{
    test_stored_credentials_win();
    test_cache_only_survives_warm_resets();
    test_ephemeral_credentials_need_a_way_to_be_read();
    return 0;
}
