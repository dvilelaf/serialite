#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_AP_SSID_MAX_BYTES 32
#define WIFI_AP_PASSWORD_MAX_BYTES 63
#define WIFI_AP_SSID_MAX_LEN (WIFI_AP_SSID_MAX_BYTES + 1)
#define WIFI_AP_PASSWORD_MAX_LEN (WIFI_AP_PASSWORD_MAX_BYTES + 1)

typedef struct {
    char ssid[WIFI_AP_SSID_MAX_LEN];
    char password[WIFI_AP_PASSWORD_MAX_LEN];
    uint8_t channel;
    uint8_t max_clients;
} kvm_wifi_ap_config_t;

typedef struct {
    char ip_addr[16];
    uint8_t connected_clients;
    bool started;
} wifi_ap_status_t;

esp_err_t wifi_ap_start(const kvm_wifi_ap_config_t *config);
esp_err_t wifi_ap_stop(void);
wifi_ap_status_t wifi_ap_get_status(void);
