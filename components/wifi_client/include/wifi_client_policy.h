#pragma once

#include <stdbool.h>
#include <stddef.h>

#define WIFI_CLIENT_POLICY_SSID_MAX 32U
#define WIFI_CLIENT_POLICY_PASSWORD_MIN 20U
#define WIFI_CLIENT_POLICY_PASSWORD_MAX 64U

typedef enum {
    WIFI_CLIENT_POLICY_ALLOW = 0,
    WIFI_CLIENT_POLICY_REJECT_DISABLED,
    WIFI_CLIENT_POLICY_REJECT_NO_PHYSICAL_PRESENCE,
    WIFI_CLIENT_POLICY_REJECT_NO_RISK_ACK,
    WIFI_CLIENT_POLICY_REJECT_UNENCRYPTED_NVS,
    WIFI_CLIENT_POLICY_REJECT_AP_DISABLED,
    WIFI_CLIENT_POLICY_REJECT_INVALID_CREDENTIALS,
} wifi_client_policy_result_t;

typedef struct {
    bool requested;
    bool physical_presence;
    bool risk_acknowledged;
    bool nvs_encrypted;
    bool ap_remains_enabled;
    const char *ssid;
    const char *password;
} wifi_client_policy_request_t;

bool wifi_client_policy_default_enabled(void);
wifi_client_policy_result_t wifi_client_policy_can_enable(const wifi_client_policy_request_t *request);
const char *wifi_client_policy_result_name(wifi_client_policy_result_t result);
