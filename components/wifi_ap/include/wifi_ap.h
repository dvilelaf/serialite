#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_AP_SSID_MAX_BYTES 32
#define WIFI_AP_PASSWORD_MAX_BYTES 63
#define WIFI_AP_SSID_MAX_LEN (WIFI_AP_SSID_MAX_BYTES + 1)
#define WIFI_AP_PASSWORD_MAX_LEN (WIFI_AP_PASSWORD_MAX_BYTES + 1)
#define WIFI_AP_MAX_CLIENTS_LIMIT 4
/* ESP-IDF's default SoftAP address. */
#define WIFI_AP_IP_ADDR "192.168.4.1"

typedef struct {
    char ssid[WIFI_AP_SSID_MAX_LEN];
    char password[WIFI_AP_PASSWORD_MAX_LEN];
    uint8_t channel;
    uint8_t max_clients;
} wifi_ap_settings_t;

typedef struct {
    char ip_addr[16];
    uint8_t connected_clients;
    bool started;
} wifi_ap_status_t;

/*
 * Starts a WPA2/WPA3 SoftAP. The passphrase stays in RAM: the WiFi driver is
 * told not to copy it into NVS.
 */
esp_err_t wifi_ap_start(const wifi_ap_settings_t *config);
esp_err_t wifi_ap_stop(void);
wifi_ap_status_t wifi_ap_get_status(void);
