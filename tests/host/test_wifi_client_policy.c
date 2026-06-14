#include "wifi_client_policy.h"

#include <assert.h>
#include <string.h>

static void disabled_by_default(void)
{
    assert(!wifi_client_policy_default_enabled());
}

static void enable_requires_explicit_physical_and_encrypted_conditions(void)
{
    const wifi_client_policy_request_t valid = {
        .requested = true,
        .physical_presence = true,
        .risk_acknowledged = true,
        .nvs_encrypted = true,
        .ap_remains_enabled = true,
        .ssid = "rack-maintenance",
        .password = "correct horse battery staple",
    };

    assert(wifi_client_policy_can_enable(&valid) == WIFI_CLIENT_POLICY_ALLOW);

    wifi_client_policy_request_t req = valid;
    req.requested = false;
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_DISABLED);

    req = valid;
    req.physical_presence = false;
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_NO_PHYSICAL_PRESENCE);

    req = valid;
    req.risk_acknowledged = false;
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_NO_RISK_ACK);

    req = valid;
    req.nvs_encrypted = false;
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_UNENCRYPTED_NVS);

    req = valid;
    req.ap_remains_enabled = false;
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_AP_DISABLED);
}

static void enable_rejects_invalid_credentials(void)
{
    wifi_client_policy_request_t req = {
        .requested = true,
        .physical_presence = true,
        .risk_acknowledged = true,
        .nvs_encrypted = true,
        .ap_remains_enabled = true,
        .ssid = "",
        .password = "correct horse battery staple",
    };
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS);

    req.ssid = "rack-maintenance";
    req.password = "short";
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS);

    char too_long_ssid[WIFI_CLIENT_POLICY_SSID_MAX + 2];
    memset(too_long_ssid, 'a', sizeof(too_long_ssid));
    too_long_ssid[sizeof(too_long_ssid) - 1] = '\0';
    req.ssid = too_long_ssid;
    req.password = "correct horse battery staple";
    assert(wifi_client_policy_can_enable(&req) == WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS);
}

static void result_names_are_stable_for_logs(void)
{
    assert(strcmp(wifi_client_policy_result_name(WIFI_CLIENT_POLICY_ALLOW), "allow") == 0);
    assert(strcmp(wifi_client_policy_result_name(WIFI_CLIENT_POLICY_REJECT_UNENCRYPTED_NVS), "unencrypted_nvs") == 0);
    assert(strcmp(wifi_client_policy_result_name((wifi_client_policy_result_t)999), "unknown") == 0);
}

int main(void)
{
    disabled_by_default();
    enable_requires_explicit_physical_and_encrypted_conditions();
    enable_rejects_invalid_credentials();
    result_names_are_stable_for_logs();
    return 0;
}
