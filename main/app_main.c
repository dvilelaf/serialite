#include "esp_err.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ble_provisioning_runtime.h"
#include "config_transfer.h"
#include "credentials.h"
#include "local_tls_identity.h"
#include "lvgl_ui.h"
#include "network_identity.h"
#include "ota_update.h"
#include "reset_control.h"
#include "storage.h"
#include "startup_policy.h"
#include "terminal_bridge.h"
#include "ui_web_url_policy.h"
#include "usb_console.h"
#include "web_server.h"
#include "web_transport_policy.h"
#include "wifi_ap.h"

static const char *TAG = "esp32_kvm";

#define EPHEMERAL_RTC_MAGIC 0x4b564d45U
#define EPHEMERAL_RTC_VERSION 1U

typedef struct {
    storage_config_t config;
    bool ui_ready;
} app_credentials_context_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    kvm_wifi_ap_config_t wifi;
    char web_password[WIFI_AP_PASSWORD_MAX_LEN];
    bool web_password_valid;
} ephemeral_rtc_cache_t;

static app_credentials_context_t s_credentials_ctx;
static RTC_NOINIT_ATTR ephemeral_rtc_cache_t s_ephemeral_rtc_cache;
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
static local_tls_identity_t s_tls_identity;
#endif

static uint32_t fnv1a_update(uint32_t hash, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t ephemeral_cache_checksum(const ephemeral_rtc_cache_t *cache)
{
    uint32_t hash = 2166136261U;
    hash = fnv1a_update(hash, &cache->magic, sizeof(cache->magic));
    hash = fnv1a_update(hash, &cache->version, sizeof(cache->version));
    hash = fnv1a_update(hash, &cache->wifi, sizeof(cache->wifi));
    hash = fnv1a_update(hash, cache->web_password, sizeof(cache->web_password));
    hash = fnv1a_update(hash, &cache->web_password_valid, sizeof(cache->web_password_valid));
    return hash;
}

static bool runtime_wifi_config_is_valid(const kvm_wifi_ap_config_t *config)
{
    if (config == NULL || config->channel == 0 || config->max_clients == 0) {
        return false;
    }

    const size_t ssid_len = strnlen(config->ssid, sizeof(config->ssid));
    const size_t password_len = strnlen(config->password, sizeof(config->password));
    return ssid_len > 0 && ssid_len < sizeof(config->ssid) &&
           password_len >= 8 && password_len < sizeof(config->password) &&
           credentials_human_phrase_matches_policy(config->password, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT);
}

static bool ephemeral_cache_valid(void)
{
    return s_ephemeral_rtc_cache.magic == EPHEMERAL_RTC_MAGIC &&
           s_ephemeral_rtc_cache.version == EPHEMERAL_RTC_VERSION &&
           s_ephemeral_rtc_cache.checksum == ephemeral_cache_checksum(&s_ephemeral_rtc_cache) &&
           runtime_wifi_config_is_valid(&s_ephemeral_rtc_cache.wifi);
}

static void ephemeral_cache_store_wifi(const kvm_wifi_ap_config_t *wifi)
{
    if (wifi == NULL) {
        return;
    }

    s_ephemeral_rtc_cache.magic = EPHEMERAL_RTC_MAGIC;
    s_ephemeral_rtc_cache.version = EPHEMERAL_RTC_VERSION;
    s_ephemeral_rtc_cache.wifi = *wifi;
    memset(s_ephemeral_rtc_cache.web_password, 0, sizeof(s_ephemeral_rtc_cache.web_password));
    s_ephemeral_rtc_cache.web_password_valid = false;
    s_ephemeral_rtc_cache.checksum = ephemeral_cache_checksum(&s_ephemeral_rtc_cache);
}

static void ephemeral_cache_store_web_password(const char *web_password)
{
    if (web_password == NULL || web_password[0] == '\0' || !ephemeral_cache_valid()) {
        return;
    }

    strlcpy(s_ephemeral_rtc_cache.web_password, web_password, sizeof(s_ephemeral_rtc_cache.web_password));
    s_ephemeral_rtc_cache.web_password_valid = true;
    s_ephemeral_rtc_cache.checksum = ephemeral_cache_checksum(&s_ephemeral_rtc_cache);
}

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

static esp_err_t generate_human_web_password(char *out, size_t out_size)
{
    const credentials_result_t result = credentials_generate_human_web_password(out, out_size, credentials_random, NULL);
    if (result == CREDENTIALS_OK) {
        return ESP_OK;
    }
    if (result == CREDENTIALS_ERR_OUTPUT_TOO_SMALL) {
        return ESP_ERR_INVALID_SIZE;
    }
    return result == CREDENTIALS_ERR_RANDOM_FAILED ? ESP_FAIL : ESP_ERR_INVALID_ARG;
}

static esp_err_t make_operational_wifi_config(
    const kvm_wifi_ap_config_t *display_config,
    kvm_wifi_ap_config_t *operational_config)
{
    if (display_config == NULL || operational_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *operational_config = *display_config;
    if (!credentials_compact_human_phrase(
            display_config->password,
            CREDENTIALS_WIFI_PASSWORD_WORD_COUNT,
            operational_config->password,
            sizeof(operational_config->password))) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

#if CONFIG_ESP32_KVM_BLE_PROVISIONING_ENABLE
static bool ble_radio_not_implemented_start(uint32_t advertising_window_seconds, void *ctx)
{
    (void)ctx;
    ESP_LOGE(TAG, "BLE provisioning radio backend is not implemented; requested window=%" PRIu32 "s", advertising_window_seconds);
    return false;
}

static void ble_radio_not_implemented_stop(void *ctx)
{
    (void)ctx;
}

static void maybe_start_ble_provisioning_runtime(bool offline_ap_available)
{
#if CONFIG_NVS_ENCRYPTION
    const bool nvs_encryption_enabled = true;
#else
    const bool nvs_encryption_enabled = false;
#endif
    static ble_provisioning_runtime_t runtime;
    ble_provisioning_runtime_init(&runtime);

    const ble_provisioning_runtime_config_t runtime_config = {
        .policy = {
            .requested = true,
            .physical_presence = false,
            .local_pairing_passed = false,
            .nvs_encrypted = nvs_encryption_enabled,
            .offline_ap_flow_available = offline_ap_available,
            .advertising_window_seconds = BLE_PROVISIONING_POLICY_ADV_WINDOW_MAX_SECONDS,
            .session_budget_seconds = BLE_PROVISIONING_POLICY_SESSION_MAX_SECONDS,
        },
        .start_radio = ble_radio_not_implemented_start,
        .stop_radio = ble_radio_not_implemented_stop,
        .radio_ctx = NULL,
    };

    const ble_provisioning_runtime_result_t result = ble_provisioning_runtime_start(&runtime, &runtime_config);
    if (result == BLE_PROVISIONING_RUNTIME_STARTED) {
        ESP_LOGW(TAG, "BLE provisioning advertising started");
    } else {
        ESP_LOGW(TAG, "BLE provisioning not started: %s", ble_provisioning_runtime_last_reason(&runtime));
    }
}
#else
static void maybe_start_ble_provisioning_runtime(bool offline_ap_available)
{
    (void)offline_ap_available;
}
#endif

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

static esp_err_t export_config_cb(char *out, size_t out_size, void *ctx)
{
    app_credentials_context_t *state = (app_credentials_context_t *)ctx;
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const config_transfer_result_t result = config_transfer_export_json(&state->config, out, out_size);
    return result == CONFIG_TRANSFER_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t import_config_cb(const char *json, void *ctx)
{
    app_credentials_context_t *state = (app_credentials_context_t *)ctx;
    if (state == NULL || json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    storage_config_t next = state->config;
    const config_transfer_result_t import_result = config_transfer_import_json(json, &next);
    if (import_result != CONFIG_TRANSFER_OK) {
        ESP_LOGW(TAG, "config import rejected: %s", config_transfer_result_name(import_result));
        return ESP_ERR_INVALID_ARG;
    }
    storage_wifi_config_apply_safe_ranges(&next.wifi);
    if (!storage_wifi_config_is_valid(&next.wifi)) {
        storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t save_err = storage_save_config(&next);
    if (save_err != ESP_OK) {
        storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
        return save_err;
    }

    storage_secure_zero(state->config.wifi.password, sizeof(state->config.wifi.password));
    state->config = next;
    storage_secure_zero(next.wifi.password, sizeof(next.wifi.password));
    return ESP_OK;
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
    if (nvs_recovered ||
        config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT ||
        config_status != STORAGE_CONFIG_STATUS_VALID ||
        !runtime_wifi_config_is_valid(&mapped_wifi_config)) {
        if (config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT) {
            ESP_LOGE(TAG, "stored AP config is corrupt; entering physical setup flow with regenerated credentials");
        }
        if (ephemeral_cache_valid()) {
            mapped_wifi_config = s_ephemeral_rtc_cache.wifi;
            ESP_LOGW(TAG, "reusing ephemeral AP credentials from RTC after software reboot");
        } else {
            ESP_ERROR_CHECK(generate_ephemeral_wifi_config(&mapped_wifi_config));
            ephemeral_cache_store_wifi(&mapped_wifi_config);
        }
        ephemeral_credentials = true;
    }
    storage_secure_zero(config.wifi.password, sizeof(config.wifi.password));

    if (!ephemeral_cache_valid()) {
        ephemeral_cache_store_wifi(&mapped_wifi_config);
    }

    char web_password[WIFI_AP_PASSWORD_MAX_LEN] = {0};
    const bool persisted_web_auth = storage_web_auth_config_is_valid(&config);
    const bool rtc_web_password_available = ephemeral_cache_valid() &&
                                            s_ephemeral_rtc_cache.web_password_valid &&
                                            credentials_human_phrase_matches_policy(
                                                s_ephemeral_rtc_cache.web_password,
                                                CREDENTIALS_WEB_PASSWORD_WORD_COUNT);
    credentials_web_auth_boot_decision_t web_auth_decision = {0};
    ESP_ERROR_CHECK(credentials_web_auth_boot_decide(
        &(credentials_web_auth_boot_input_t){
            .persisted_hash_configured = persisted_web_auth,
            .rtc_password_available = rtc_web_password_available,
        },
        &web_auth_decision) ? ESP_OK : ESP_ERR_INVALID_STATE);

    if (web_auth_decision.use_rtc_password) {
        strlcpy(web_password, s_ephemeral_rtc_cache.web_password, sizeof(web_password));
        ESP_LOGW(TAG, "reusing local-display web password from RTC after software reboot");
    } else if (web_auth_decision.generate_runtime_password) {
        ESP_ERROR_CHECK(generate_human_web_password(web_password, sizeof(web_password)));
        ephemeral_cache_store_web_password(web_password);
        if (persisted_web_auth) {
            ESP_LOGW(TAG, "stored web auth exists, but local-display runtime password will be used for this boot");
        }
    }

    bool tls_ready = false;
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
    const local_tls_identity_result_t tls_result = local_tls_identity_generate(
        &s_tls_identity,
        credentials_random,
        NULL,
        "kvm.local");
    tls_ready = tls_result == LOCAL_TLS_IDENTITY_OK;
    if (!tls_ready) {
        ESP_LOGW(TAG, "HTTPS identity unavailable despite HTTPS opt-in: %s; falling back to HTTP", local_tls_identity_result_name(tls_result));
    }
#endif

    const lvgl_ui_boot_status_t ui_status = {
        .ssid = mapped_wifi_config.ssid,
        .password = mapped_wifi_config.password,
        .web_password = web_password,
        .https_fingerprint =
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
            tls_ready ? s_tls_identity.fingerprint_text : NULL,
#else
            NULL,
#endif
        .web_url = ui_web_url_for_transport(tls_ready),
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
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
        local_tls_identity_zeroize(&s_tls_identity);
#endif
        const esp_err_t usb_err = usb_console_start();
        log_init_result("usb_console", usb_err);
        return;
    }

    kvm_wifi_ap_config_t operational_wifi_config = {0};
    ESP_ERROR_CHECK(make_operational_wifi_config(&mapped_wifi_config, &operational_wifi_config));
    esp_err_t wifi_err = wifi_ap_start(&operational_wifi_config);
    log_init_result("wifi_ap", wifi_err);
    maybe_start_ble_provisioning_runtime(wifi_err == ESP_OK);
    storage_secure_zero(operational_wifi_config.password, sizeof(operational_wifi_config.password));
    storage_secure_zero(mapped_wifi_config.password, sizeof(mapped_wifi_config.password));
    const esp_err_t usb_err = usb_console_start();
    log_init_result("usb_console", usb_err);
    if (wifi_err == ESP_OK) {
        const web_server_config_t web_config = {
            .web_password = web_auth_decision.use_persisted_hash ? NULL : web_password,
            .web_password_salt = web_auth_decision.use_persisted_hash ? config.web_password_salt : NULL,
            .web_password_hash = web_auth_decision.use_persisted_hash ? config.web_password_hash : NULL,
            .web_password_hash_configured = web_auth_decision.use_persisted_hash,
            .tls_identity =
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
                tls_ready ? &s_tls_identity : NULL,
#else
                NULL,
#endif
            .tls_fingerprint_displayed_locally = tls_ready && s_credentials_ctx.ui_ready,
            .rotate_credentials = rotate_credentials_cb,
            .rotate_credentials_ctx = &s_credentials_ctx,
            .export_config = export_config_cb,
            .import_config = import_config_cb,
            .config_ctx = &s_credentials_ctx,
        };
        const esp_err_t web_err = web_server_start(&web_config);
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
        local_tls_identity_zeroize(&s_tls_identity);
#endif
        storage_secure_zero(web_password, sizeof(web_password));
        log_init_result("web_server", web_err);
        const web_server_status_t web_status = web_server_get_status();
        const web_transport_t web_transport = web_transport_from_status(web_err == ESP_OK && web_status.started, web_status.tls_active);
        if (web_transport_should_advertise_mdns(web_transport)) {
            const network_identity_config_t network_identity_config = {
                .hostname = NETWORK_IDENTITY_HOSTNAME,
                .instance_name = NETWORK_IDENTITY_SERVICE_NAME,
                .service_type = web_transport == WEB_TRANSPORT_HTTPS ? NETWORK_IDENTITY_HTTPS_SERVICE : NETWORK_IDENTITY_HTTP_SERVICE,
                .port = web_transport == WEB_TRANSPORT_HTTPS ? 443 : 80,
                .ttl_seconds = 120,
            };
            const esp_err_t identity_err = network_identity_start(&network_identity_config);
            if (identity_err != ESP_OK) {
                if (web_transport == WEB_TRANSPORT_HTTPS) {
                    ESP_LOGW(TAG, "mDNS unavailable for trusted HTTPS name %s: %s", NETWORK_IDENTITY_LOCAL_HTTPS_URL, esp_err_to_name(identity_err));
                } else {
                    ESP_LOGW(TAG, "mDNS unavailable; use %s://192.168.4.1: %s", web_transport_scheme(web_transport), esp_err_to_name(identity_err));
                }
            }
        }
        if (bridge_err == ESP_OK && wifi_err == ESP_OK) {
            log_init_result("ota_valid", ota_update_mark_running_app_valid());
        } else {
            ESP_LOGW(TAG, "running app not marked valid for OTA rollback because AP/bridge rescue services are not ready");
        }
        if (startup_policy_after_web(true, web_err == ESP_OK) == STARTUP_POLICY_CONTINUE && web_err != ESP_OK) {
            ESP_LOGE(TAG, "web/auth service failed after WiFi startup; keeping AP alive for rescue diagnostics");
        }
    } else {
#if CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE
        local_tls_identity_zeroize(&s_tls_identity);
#endif
        storage_secure_zero(web_password, sizeof(web_password));
        ESP_LOGE(TAG, "web_server skipped because AP did not start");
    }
}
