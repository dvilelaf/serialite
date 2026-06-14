#include "storage_config.h"

#include <string.h>

static bool bounded_strlen(const char *value, size_t max_len, size_t *out_len)
{
    if (value == NULL) {
        return false;
    }

    size_t len = 0;
    while (len <= max_len && value[len] != '\0') {
        len++;
    }

    if (len > max_len) {
        return false;
    }

    if (out_len != NULL) {
        *out_len = len;
    }
    return true;
}

bool storage_wifi_config_is_valid(const storage_wifi_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    size_t ssid_len = 0;
    size_t password_len = 0;
    if (!bounded_strlen(config->ssid, STORAGE_SSID_MAX_BYTES, &ssid_len) ||
        !bounded_strlen(config->password, STORAGE_PASSWORD_MAX_BYTES, &password_len)) {
        return false;
    }

    if (ssid_len == 0 || password_len < STORAGE_PRODUCTION_PASSWORD_MIN_BYTES) {
        return false;
    }

    if (config->channel < 1 || config->channel > 13) {
        return false;
    }

    if (config->max_clients < 1 || config->max_clients > 4) {
        return false;
    }

    return true;
}

bool storage_web_auth_config_is_valid(const storage_config_t *config)
{
    if (config == NULL || !config->web_password_hash_configured) {
        return false;
    }

    uint8_t salt_or = 0;
    uint8_t hash_or = 0;
    for (size_t i = 0; i < STORAGE_WEB_PASSWORD_SALT_LEN; ++i) {
        salt_or |= config->web_password_salt[i];
    }
    for (size_t i = 0; i < STORAGE_WEB_PASSWORD_HASH_LEN; ++i) {
        hash_or |= config->web_password_hash[i];
    }
    return salt_or != 0 && hash_or != 0;
}

storage_config_status_t storage_wifi_config_classify(const storage_wifi_config_t *config)
{
    if (config == NULL) {
        return STORAGE_CONFIG_STATUS_CORRUPT;
    }

    size_t ssid_len = 0;
    size_t password_len = 0;
    if (!bounded_strlen(config->ssid, STORAGE_SSID_MAX_BYTES, &ssid_len) ||
        !bounded_strlen(config->password, STORAGE_PASSWORD_MAX_BYTES, &password_len)) {
        return STORAGE_CONFIG_STATUS_CORRUPT;
    }

    if (ssid_len == 0 && password_len == 0) {
        return STORAGE_CONFIG_STATUS_MISSING;
    }

    return storage_wifi_config_is_valid(config) ? STORAGE_CONFIG_STATUS_VALID : STORAGE_CONFIG_STATUS_CORRUPT;
}

bool storage_config_secret_persistence_allowed(bool nvs_encryption_enabled)
{
    return nvs_encryption_enabled;
}

bool storage_config_should_scrub_legacy_plaintext_secrets(bool nvs_encryption_enabled)
{
    return !nvs_encryption_enabled;
}

bool storage_config_scrub_legacy_plaintext_secrets(
    bool nvs_encryption_enabled,
    storage_secret_erase_fn_t erase_fn,
    storage_secret_commit_fn_t commit_fn,
    void *ctx)
{
    if (!storage_config_should_scrub_legacy_plaintext_secrets(nvs_encryption_enabled)) {
        return true;
    }
    if (erase_fn == NULL || commit_fn == NULL) {
        return false;
    }

    bool changed = false;
    const storage_secret_erase_result_t ssid_result = erase_fn(STORAGE_SECRET_KEY_SSID, ctx);
    if (ssid_result == STORAGE_SECRET_ERASE_ERROR) {
        return false;
    }
    changed = changed || ssid_result == STORAGE_SECRET_ERASED;

    const storage_secret_erase_result_t password_result = erase_fn(STORAGE_SECRET_KEY_PASSWORD, ctx);
    if (password_result == STORAGE_SECRET_ERASE_ERROR) {
        return false;
    }
    changed = changed || password_result == STORAGE_SECRET_ERASED;

    return !changed || commit_fn(ctx);
}

void storage_secure_zero(void *ptr, size_t len)
{
    if (ptr == NULL) {
        return;
    }

    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

void storage_wifi_config_apply_safe_ranges(storage_wifi_config_t *config)
{
    if (config == NULL) {
        return;
    }

    if (config->channel < 1 || config->channel > 13) {
        config->channel = 6;
    }

    if (config->max_clients < 1 || config->max_clients > 4) {
        config->max_clients = 4;
    }
}
