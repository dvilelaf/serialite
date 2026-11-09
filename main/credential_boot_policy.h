#pragma once

/* Which AP credentials to use at boot. Pure logic, tested on the host. */

#include <stdbool.h>

typedef enum {
    /* Credentials persisted in encrypted NVS. */
    CREDENTIAL_SOURCE_STORED = 0,
    /* The ephemeral credentials from before a software restart, kept in RTC memory. */
    CREDENTIAL_SOURCE_RTC_CACHE,
    /* Fresh ephemeral credentials. */
    CREDENTIAL_SOURCE_GENERATE,
} credential_source_t;

typedef struct {
    bool stored_valid;
    bool rtc_cache_valid;
    /* The reset kept RTC memory on purpose: software restart, panic or watchdog. */
    bool warm_reset;
} credential_boot_facts_t;

credential_source_t credential_boot_policy_select(const credential_boot_facts_t *facts);
/*
 * Ephemeral credentials are only useful if someone can read them, so without
 * a display the AP only starts with stored credentials, unless the build
 * prints them on the local UART.
 */
bool credential_boot_policy_can_start_ap(credential_source_t source, bool display_running, bool uart_reveal_enabled);
