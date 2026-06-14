#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#include "credentials.h"
#include "local_pairing.h"
#include "lvgl_ui.h"
#include "network_identity.h"
#include "ota_update.h"
#include "reset_control.h"
#include "storage.h"
#include "startup_policy.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "esp32_kvm";

typedef struct {
    storage_config_t config;
    bool ui_ready;
} app_credentials_context_t;

static app_credentials_context_t s_credentials_ctx;

static bool credentials_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    if (buf == NULL) {
        return false;
    }
    esp_fill_random(buf, len);
    return true;
}

static void log_init_result(const char *name, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s ready", name);
        return;
    }

    ESP_LOGE(TAG, "%s failed: %s", name, esp_err_to_name(err));
}

static esp_err_t init_nvs(bool *recovered)
{
    if (recovered == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *recovered = false;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        *recovered = true;
        err = nvs_flash_init();
    }

    return err;
}

static esp_err_t generate_human_password(char *out, size_t out_size)
{
    const credentials_result_t result = credentials_generate_human_password(out, out_size, credentials_random, NULL);
    if (result == CREDENTIALS_OK) {
        return ESP_OK;
    }
    if (result == CREDENTIALS_ERR_OUTPUT_TOO_SMALL) {
        return ESP_ERR_INVALID_SIZE;
    }
    return result == CREDENTIALS_ERR_RANDOM_FAILED ? ESP_FAIL : ESP_ERR_INVALID_ARG;
}

static esp_err_t generate_pairing_code(char out[LOCAL_PAIRING_CODE_BUF_LEN])
{
    const local_pairing_result_t result = local_pairing_generate_code(out, credentials_random, NULL);
    if (result == LOCAL_PAIRING_OK) {
        return ESP_OK;
    }
    return result == LOCAL_PAIRING_ERR_RANDOM_FAILED ? ESP_FAIL : ESP_ERR_INVALID_ARG;
}

static esp_err_t rotate_credentials_cb(const web_server_credential_rotation_t *rotation, void *ctx)
{
    app_credentials_context_t *state = (app_credentials_context_t *)ctx;
    if (rotation == NULL || state == NULL || rotation->wifi_password == NULL || rotation->web_password == NULL ||
        rotation->web_password_salt == NULL || rotation->web_password_hash == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_NVS_ENCRYPTION
    const bool nvs_encryption_enabled = true;
#else
    const bool nvs_encryption_enabled = false;
#endif
    const credential_rotation_policy_result_t policy = credential_rotation_policy_evaluate(
        state->ui_ready,
        storage_config_secret_persistence_allowed(nvs_encryption_enabled));
    if (policy != CREDENTIAL_ROTATION_ACCEPT) {
        ESP_LOGW(TAG, "credential rotation rejected: %s", credential_rotation_policy_result_name(policy));
        return ESP_ERR_INVALID_STATE;
    }

    storage_config_t next = state->config;
    strlcpy(next.wifi.ssid, "KVM", sizeof(next.wifi.ssid));
    strlcpy(next.wifi.password, rotation->wifi_password, sizeof(next.wifi.password));
    memcpy(next.web_password_salt, rotation->web_password_salt, sizeof(next.web_password_salt));
    memcpy(next.web_password_hash, rotation->web_password_hash, sizeof(next.web_password_hash));
    next.web_password_hash_configured = true;
    storage_wifi_config_apply_safe_ranges(&next.wifi);
    if (!storage_wifi_config_is_valid(&next.wifi)) {
        storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
        return ESP_ERR_INVALID_ARG;
    }

    const lvgl_ui_boot_status_t ui_status = {
        .ssid = next.wifi.ssid,
        .password = rotation->wifi_password,
        .web_password = rotation->web_password,
        .ip_addr = "192.168.4.1",
        .usb_connected = usb_console_get_status().connected,
    };
    ESP_RETURN_ON_ERROR(lvgl_ui_update_credentials(&ui_status, false), TAG, "local credential display staging failed");

    const esp_err_t save_err = storage_save_config(&next);
    if (save_err != ESP_OK) {
        const lvgl_ui_boot_status_t rollback_ui_status = {
            .ssid = state->config.wifi.ssid,
            .password = state->config.wifi.password,
            .web_password = "current web password unchanged",
            .ip_addr = "192.168.4.1",
            .usb_connected = usb_console_get_status().connected,
        };
        (void)lvgl_ui_update_credentials(&rollback_ui_status, false);
        storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
        return save_err;
    }

    const esp_err_t reveal_err = lvgl_ui_update_credentials(&ui_status, rotation->reveal_on_local_display);
    if (reveal_err != ESP_OK) {
        ESP_LOGW(TAG, "rotated credentials stored but immediate reveal failed: %s", esp_err_to_name(reveal_err));
    }

    storage_secure_zero(state->config.wifi.password, sizeof(state->config.wifi.password));
    state->config = next;
    storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
    return ESP_OK;
}

static void pairing_event_cb(web_server_pairing_event_t event, void *ctx)
{
    app_credentials_context_t *state = (app_credentials_context_t *)ctx;
    if (state == NULL || !state->ui_ready) {
        return;
    }

    const char *status = event == WEB_SERVER_PAIRING_LOCKED ? "Locked" : "Used";
    const esp_err_t err = lvgl_ui_set_pairing_status(status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pairing status update failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t generate_ephemeral_wifi_config(kvm_wifi_ap_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(config->ssid, "KVM", sizeof(config->ssid));

    ESP_RETURN_ON_ERROR(generate_human_password(config->password, sizeof(config->password)), TAG, "AP password generation failed");
    config->channel = 6;
    config->max_clients = 4;

    ESP_LOGW(TAG, "generated ephemeral AP credentials for ssid=%s", config->ssid);
    ESP_LOGW(TAG, "AP password is not logged; local display flow will expose it during provisioning");
    return ESP_OK;
}

void app_main(void)
{
    bool nvs_recovered = false;
    ESP_ERROR_CHECK(init_nvs(&nvs_recovered));

    storage_config_t config;
    const esp_err_t storage_err = storage_init();
    log_init_result("storage", storage_err);
    ESP_ERROR_CHECK(storage_err);
    const esp_err_t bridge_err = terminal_bridge_start();
    log_init_result("terminal_bridge", bridge_err);
    const esp_err_t config_err = storage_load_config(&config);
    if (config_err != ESP_OK && config_err != ESP_ERR_STORAGE_CONFIG_CORRUPT) {
        ESP_ERROR_CHECK(config_err);
    }

    kvm_wifi_ap_config_t mapped_wifi_config = {
        .channel = config.wifi.channel,
        .max_clients = config.wifi.max_clients,
    };
    strlcpy(mapped_wifi_config.ssid, config.wifi.ssid, sizeof(mapped_wifi_config.ssid));
    strlcpy(mapped_wifi_config.password, config.wifi.password, sizeof(mapped_wifi_config.password));

    bool ephemeral_credentials = false;
    const storage_config_status_t config_status = storage_wifi_config_classify(&config.wifi);
    if (nvs_recovered || config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT || config_status != STORAGE_CONFIG_STATUS_VALID) {
        if (config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT) {
            ESP_LOGE(TAG, "stored AP config is corrupt; entering physical setup flow with regenerated credentials");
        }
        ESP_ERROR_CHECK(generate_ephemeral_wifi_config(&mapped_wifi_config));
        ephemeral_credentials = true;
    }
    storage_secure_zero(config.wifi.password, sizeof(config.wifi.password));

    char web_password[WIFI_AP_PASSWORD_MAX_LEN] = {0};
    const bool persisted_web_auth = storage_web_auth_config_is_valid(&config);
    if (!persisted_web_auth) {
        ESP_ERROR_CHECK(generate_human_password(web_password, sizeof(web_password)));
    } else {
        strlcpy(web_password, "stored - use rotated password", sizeof(web_password));
    }

    char pairing_code[LOCAL_PAIRING_CODE_BUF_LEN] = {0};
    ESP_ERROR_CHECK(generate_pairing_code(pairing_code));

    const lvgl_ui_boot_status_t ui_status = {
        .ssid = mapped_wifi_config.ssid,
        .password = mapped_wifi_config.password,
        .web_password = web_password,
        .pairing_code = pairing_code,
        .ip_addr = "192.168.4.1",
        .usb_connected = false,
    };
    const esp_err_t ui_err = lvgl_ui_start(&ui_status);
    log_init_result("lvgl_ui", ui_err);
    s_credentials_ctx.config = config;
    s_credentials_ctx.ui_ready = ui_err == ESP_OK;
    strlcpy(s_credentials_ctx.config.wifi.ssid, mapped_wifi_config.ssid, sizeof(s_credentials_ctx.config.wifi.ssid));
    strlcpy(s_credentials_ctx.config.wifi.password, mapped_wifi_config.password, sizeof(s_credentials_ctx.config.wifi.password));
    s_credentials_ctx.config.wifi.channel = mapped_wifi_config.channel;
    s_credentials_ctx.config.wifi.max_clients = mapped_wifi_config.max_clients;
    if (ui_err == ESP_OK) {
        log_init_result("reset_control", reset_control_start());
    }
    if (startup_policy_after_ui(ui_err == ESP_OK, ephemeral_credentials) == STARTUP_POLICY_SKIP_AP) {
        ESP_LOGE(TAG, "AP skipped: ephemeral password cannot be safely exposed without local display");
        storage_secure_zero(mapped_wifi_config.password, sizeof(mapped_wifi_config.password));
        storage_secure_zero(web_password, sizeof(web_password));
        storage_secure_zero(pairing_code, sizeof(pairing_code));
        const esp_err_t usb_err = usb_console_start();
        log_init_result("usb_console", usb_err);
        return;
    }

    esp_err_t wifi_err = wifi_ap_start(&mapped_wifi_config);
    log_init_result("wifi_ap", wifi_err);
    storage_secure_zero(mapped_wifi_config.password, sizeof(mapped_wifi_config.password));
    const esp_err_t usb_err = usb_console_start();
    log_init_result("usb_console", usb_err);
    if (wifi_err == ESP_OK) {
        const network_identity_config_t network_identity_config = {
            .hostname = NETWORK_IDENTITY_HOSTNAME,
            .instance_name = NETWORK_IDENTITY_SERVICE_NAME,
            .service_type = NETWORK_IDENTITY_HTTP_SERVICE,
            .port = 80,
            .ttl_seconds = 120,
        };
        const esp_err_t identity_err = network_identity_start(&network_identity_config);
        if (identity_err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS unavailable; use http://192.168.4.1: %s", esp_err_to_name(identity_err));
        }
        const web_server_config_t web_config = {
            .web_password = persisted_web_auth ? NULL : web_password,
            .web_password_salt = persisted_web_auth ? config.web_password_salt : NULL,
            .web_password_hash = persisted_web_auth ? config.web_password_hash : NULL,
            .web_password_hash_configured = persisted_web_auth,
            .pairing_code = pairing_code,
            .rotate_credentials = rotate_credentials_cb,
            .rotate_credentials_ctx = &s_credentials_ctx,
            .pairing_event = pairing_event_cb,
            .pairing_event_ctx = &s_credentials_ctx,
        };
        const esp_err_t web_err = web_server_start(&web_config);
        storage_secure_zero(web_password, sizeof(web_password));
        storage_secure_zero(pairing_code, sizeof(pairing_code));
        log_init_result("web_server", web_err);
        if (bridge_err == ESP_OK && usb_err == ESP_OK && wifi_err == ESP_OK && web_err == ESP_OK) {
            log_init_result("ota_valid", ota_update_mark_running_app_valid());
        } else {
            ESP_LOGW(TAG, "running app not marked valid for OTA rollback because critical services are not all ready");
        }
        if (startup_policy_after_web(true, web_err == ESP_OK) == STARTUP_POLICY_STOP_AP) {
            ESP_LOGE(TAG, "AP stopped: web/auth service failed after WiFi startup");
            log_init_result("wifi_ap_stop", wifi_ap_stop());
        }
    } else {
        storage_secure_zero(web_password, sizeof(web_password));
        storage_secure_zero(pairing_code, sizeof(pairing_code));
        ESP_LOGE(TAG, "web_server skipped because AP did not start");
    }
}
