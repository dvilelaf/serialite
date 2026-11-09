#include "credential_boot_policy.h"

#include <stddef.h>

credential_source_t credential_boot_policy_select(const credential_boot_facts_t *facts)
{
    if (facts == NULL) {
        return CREDENTIAL_SOURCE_GENERATE;
    }
    if (facts->stored_valid) {
        return CREDENTIAL_SOURCE_STORED;
    }
    if (facts->rtc_cache_valid && facts->warm_reset) {
        return CREDENTIAL_SOURCE_RTC_CACHE;
    }
    return CREDENTIAL_SOURCE_GENERATE;
}

bool credential_boot_policy_can_start_ap(credential_source_t source, bool display_running, bool uart_reveal_enabled)
{
    return source == CREDENTIAL_SOURCE_STORED || display_running || uart_reveal_enabled;
}
