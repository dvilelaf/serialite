#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STORAGE_SSID_MAX_BYTES 32
#define STORAGE_PASSWORD_MAX_BYTES 63
#define STORAGE_PRODUCTION_PASSWORD_MIN_BYTES 20
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

typedef enum {
    STORAGE_CONFIG_STATUS_MISSING = 0,
    STORAGE_CONFIG_STATUS_VALID,
    STORAGE_CONFIG_STATUS_CORRUPT,
} storage_config_status_t;

typedef enum {
    STORAGE_SECRET_KEY_SSID = 0,
    STORAGE_SECRET_KEY_PASSWORD,
} storage_secret_key_t;

typedef enum {
    STORAGE_SECRET_ERASED = 0,
    STORAGE_SECRET_NOT_FOUND,
    STORAGE_SECRET_ERASE_ERROR,
} storage_secret_erase_result_t;

typedef storage_secret_erase_result_t (*storage_secret_erase_fn_t)(storage_secret_key_t key, void *ctx);
typedef bool (*storage_secret_commit_fn_t)(void *ctx);

bool storage_wifi_config_is_valid(const storage_wifi_config_t *config);
void storage_wifi_config_apply_safe_ranges(storage_wifi_config_t *config);
storage_config_status_t storage_wifi_config_classify(const storage_wifi_config_t *config);
bool storage_config_secret_persistence_allowed(bool nvs_encryption_enabled);
bool storage_config_should_scrub_legacy_plaintext_secrets(bool nvs_encryption_enabled);
bool storage_config_scrub_legacy_plaintext_secrets(
    bool nvs_encryption_enabled,
    storage_secret_erase_fn_t erase_fn,
    storage_secret_commit_fn_t commit_fn,
    void *ctx);
void storage_secure_zero(void *ptr, size_t len);
