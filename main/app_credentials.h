#pragma once

/* Where the AP credentials come from, how they rotate and how they are forgotten. */

#include <stddef.h>
#include <stdint.h>

#include "credential_boot_policy.h"
#include "esp_err.h"
#include "storage_config.h"
#include "wifi_ap.h"

typedef struct {
    char ssid[STORAGE_SSID_MAX_LEN];
    /* Space-separated words, as shown on the screen. */
    char phrase[STORAGE_PASSWORD_MAX_LEN];
    uint8_t channel;
    credential_source_t source;
} app_credentials_t;

/*
 * Picks stored, cached or new credentials. May generate a password, so call
 * it while the hardware entropy source is on (before WiFi starts).
 */
esp_err_t app_credentials_load(app_credentials_t *out, bool storage_available);
/* The SoftAP config; the WPA passphrase is the phrase without spaces. */
esp_err_t app_credentials_ap_config(const app_credentials_t *creds, uint8_t max_clients, wifi_ap_settings_t *out);
/* New password: persisted in encrypted NVS when possible, else cached in RTC memory for the restart. */
esp_err_t app_credentials_rotate(app_credentials_t *creds);
esp_err_t app_credentials_export(const app_credentials_t *creds, char *out, size_t out_size);
/* ESP_ERR_INVALID_ARG for a bad document, ESP_ERR_NOT_SUPPORTED when nothing can be persisted. */
esp_err_t app_credentials_import(app_credentials_t *creds, const char *json);
/* Drops the RTC copy so the next boot cannot reuse the current password. */
void app_credentials_forget_cached(void);
void app_credentials_wipe(app_credentials_t *creds);
