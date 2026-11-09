#include "storage.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "storage";
static const char *NVS_NAMESPACE = "serialite";
static const char *CONFIG_KEY = "config";

#define STORAGE_RECORD_VERSION 2U

/* One blob keeps the save atomic: NVS either sees the old record or the new one. */
typedef struct {
    uint8_t version;
    uint8_t channel;
    char ssid[STORAGE_SSID_MAX_LEN];
    char password[STORAGE_PASSWORD_MAX_LEN];
} storage_record_t;

/* Keys written by earlier firmware, some of them plaintext secrets. */
static const char *const LEGACY_KEYS[] = {
    "ssid", "password", "channel", "max_clients", "web_salt", "web_hash", "brightness", "font_size",
};

static bool nvs_encryption_enabled(void)
{
#if CONFIG_NVS_ENCRYPTION
    return true;
#else
    return false;
#endif
}

static void storage_set_defaults(storage_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->wifi.channel = STORAGE_WIFI_CHANNEL_DEFAULT;
}

static esp_err_t erase_legacy_keys(nvs_handle_t handle)
{
    bool changed = false;
    for (size_t i = 0; i < sizeof(LEGACY_KEYS) / sizeof(LEGACY_KEYS[0]); ++i) {
        const esp_err_t err = nvs_erase_key(handle, LEGACY_KEYS[i]);
        if (err == ESP_OK) {
            changed = true;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }
    if (changed) {
        ESP_LOGW(TAG, "erased configuration keys left by older firmware");
        return nvs_commit(handle);
    }
    return ESP_OK;
}

esp_err_t storage_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = erase_legacy_keys(handle);
    if (err == ESP_OK && !storage_config_secret_persistence_allowed(nvs_encryption_enabled())) {
        err = nvs_erase_key(handle, CONFIG_KEY);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_config(storage_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    storage_set_defaults(config);
    if (!storage_config_secret_persistence_allowed(nvs_encryption_enabled())) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    storage_record_t record;
    size_t len = sizeof(record);
    err = nvs_get_blob(handle, CONFIG_KEY, &record, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK || len != sizeof(record) || record.version != STORAGE_RECORD_VERSION) {
        storage_secure_zero(&record, sizeof(record));
        return ESP_ERR_STORAGE_CONFIG_CORRUPT;
    }

    memcpy(config->wifi.ssid, record.ssid, sizeof(config->wifi.ssid));
    memcpy(config->wifi.password, record.password, sizeof(config->wifi.password));
    config->wifi.ssid[sizeof(config->wifi.ssid) - 1] = '\0';
    config->wifi.password[sizeof(config->wifi.password) - 1] = '\0';
    config->wifi.channel = record.channel;
    storage_secure_zero(&record, sizeof(record));

    if (storage_wifi_config_classify(&config->wifi) != STORAGE_CONFIG_STATUS_VALID) {
        storage_secure_zero(config, sizeof(*config));
        storage_set_defaults(config);
        return ESP_ERR_STORAGE_CONFIG_CORRUPT;
    }
    return ESP_OK;
}

esp_err_t storage_save_config(const storage_config_t *config)
{
    if (config == NULL || !storage_wifi_config_is_valid(&config->wifi)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!storage_config_secret_persistence_allowed(nvs_encryption_enabled())) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    storage_record_t record = {
        .version = STORAGE_RECORD_VERSION,
        .channel = config->wifi.channel,
    };
    memcpy(record.ssid, config->wifi.ssid, sizeof(record.ssid));
    memcpy(record.password, config->wifi.password, sizeof(record.password));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, CONFIG_KEY, &record, sizeof(record));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    storage_secure_zero(&record, sizeof(record));
    return err;
}

esp_err_t storage_factory_reset(void)
{
    /*
     * Erase the whole partition, not just our namespace: older firmware let
     * the WiFi driver keep the AP passphrase in its own namespace.
     */
    (void)nvs_flash_deinit();
    return nvs_flash_erase();
}
