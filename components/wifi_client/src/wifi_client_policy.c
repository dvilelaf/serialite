#include "wifi_client_policy.h"

static bool bounded_non_empty(const char *value, size_t max_len, size_t *out_len)
{
    if (value == NULL) {
        return false;
    }

    size_t len = 0;
    while (len <= max_len && value[len] != '\0') {
        if ((unsigned char)value[len] < 0x20U) {
            return false;
        }
        len++;
    }
    if (len == 0 || len > max_len) {
        return false;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return true;
}

bool wifi_client_policy_default_enabled(void)
{
    return false;
}

wifi_client_policy_result_t wifi_client_policy_can_enable(const wifi_client_policy_request_t *request)
{
    if (request == NULL || !request->requested) {
        return WIFI_CLIENT_POLICY_REJECT_DISABLED;
    }
    if (!request->physical_presence) {
        return WIFI_CLIENT_POLICY_REJECT_NO_PHYSICAL_PRESENCE;
    }
    if (!request->risk_acknowledged) {
        return WIFI_CLIENT_POLICY_REJECT_NO_RISK_ACK;
    }
    if (!request->nvs_encrypted) {
        return WIFI_CLIENT_POLICY_REJECT_UNENCRYPTED_NVS;
    }
    if (!request->ap_remains_enabled) {
        return WIFI_CLIENT_POLICY_REJECT_AP_DISABLED;
    }

    size_t password_len = 0;
    if (!bounded_non_empty(request->ssid, WIFI_CLIENT_POLICY_SSID_MAX, NULL) ||
        !bounded_non_empty(request->password, WIFI_CLIENT_POLICY_PASSWORD_MAX, &password_len) ||
        password_len < WIFI_CLIENT_POLICY_PASSWORD_MIN) {
        return WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS;
    }

    return WIFI_CLIENT_POLICY_ALLOW;
}

const char *wifi_client_policy_result_name(wifi_client_policy_result_t result)
{
    switch (result) {
        case WIFI_CLIENT_POLICY_ALLOW:
            return "allow";
        case WIFI_CLIENT_POLICY_REJECT_DISABLED:
            return "disabled";
        case WIFI_CLIENT_POLICY_REJECT_NO_PHYSICAL_PRESENCE:
            return "no_physical_presence";
        case WIFI_CLIENT_POLICY_REJECT_NO_RISK_ACK:
            return "no_risk_ack";
        case WIFI_CLIENT_POLICY_REJECT_UNENCRYPTED_NVS:
            return "unencrypted_nvs";
        case WIFI_CLIENT_POLICY_REJECT_AP_DISABLED:
            return "ap_disabled";
        case WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS:
            return "invalid_credentials";
        default:
            return "unknown";
    }
}
