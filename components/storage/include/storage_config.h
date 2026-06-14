#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_SSID_MAX_BYTES 32
#define STORAGE_PASSWORD_MAX_BYTES 63
#define STORAGE_SSID_MAX_LEN (STORAGE_SSID_MAX_BYTES + 1)
#define STORAGE_PASSWORD_MAX_LEN (STORAGE_PASSWORD_MAX_BYTES + 1)

typedef struct {
    char ssid[STORAGE_SSID_MAX_LEN];
    char password[STORAGE_PASSWORD_MAX_LEN];
    uint8_t channel;
    uint8_t max_clients;
} storage_wifi_config_t;

typedef struct {
    storage_wifi_config_t wifi;
    uint8_t brightness;
    uint8_t font_size;
} storage_config_t;

bool storage_wifi_config_is_valid(const storage_wifi_config_t *config);
void storage_wifi_config_apply_safe_ranges(storage_wifi_config_t *config);
