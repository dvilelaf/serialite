#include "storage.h"

#include <string.h>

#include "nvs.h"
#include "sdkconfig.h"

static const char *NVS_NAMESPACE = "esp32_kvm";

static void storage_set_defaults(storage_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->wifi.channel = 6;
    config->wifi.max_clients = 4;
    config->brightness = 80;
    config->font_size = 14;
}

esp_err_t storage_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t storage_load_config(storage_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    storage_set_defaults(config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_NVS_ENCRYPTION
    size_t len = sizeof(config->wifi.ssid);
    const esp_err_t ssid_err = nvs_get_str(handle, "ssid", config->wifi.ssid, &len);
    len = sizeof(config->wifi.password);
    const esp_err_t password_err = nvs_get_str(handle, "password", config->wifi.password, &len);
    if ((ssid_err == ESP_OK && password_err != ESP_OK) ||
        (ssid_err != ESP_OK && password_err == ESP_OK) ||
        (ssid_err != ESP_OK && ssid_err != ESP_ERR_NVS_NOT_FOUND) ||
        (password_err != ESP_OK && password_err != ESP_ERR_NVS_NOT_FOUND)) {
        nvs_close(handle);
        storage_set_defaults(config);
        return ESP_ERR_STORAGE_CONFIG_CORRUPT;
    }
#endif
    (void)nvs_get_u8(handle, "channel", &config->wifi.channel);
    (void)nvs_get_u8(handle, "max_clients", &config->wifi.max_clients);
    (void)nvs_get_u8(handle, "brightness", &config->brightness);
    (void)nvs_get_u8(handle, "font_size", &config->font_size);

    const storage_config_status_t status = storage_wifi_config_classify(&config->wifi);
    if (status == STORAGE_CONFIG_STATUS_CORRUPT) {
        nvs_close(handle);
        storage_set_defaults(config);
        return ESP_ERR_STORAGE_CONFIG_CORRUPT;
    }
    if (status == STORAGE_CONFIG_STATUS_MISSING) {
        storage_wifi_config_apply_safe_ranges(&config->wifi);
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t storage_save_config(const storage_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!storage_wifi_config_is_valid(&config->wifi)) {
        return ESP_ERR_INVALID_ARG;
    }

#if !CONFIG_NVS_ENCRYPTION
    return ESP_ERR_INVALID_STATE;
#endif

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "ssid", config->wifi.ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "password", config->wifi.password);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "channel", config->wifi.channel);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "max_clients", config->wifi.max_clients);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "brightness", config->brightness);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "font_size", config->font_size);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t storage_factory_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
