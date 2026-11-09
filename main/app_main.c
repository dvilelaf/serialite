/*
 * Serialite boot sequence. Every step degrades instead of aborting: the USB
 * serial bridge must keep running even when storage, the display or WiFi
 * fail, because this is the device people reach for when things are broken.
 */

#include <string.h>

#include "ap_guard.h"
#include "app_credentials.h"
#include "board.h"
#include "bootloader_random.h"
#include "buttons.h"
#include "credential_boot_policy.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "event_log.h"
#include "https_fingerprint.h"
#include "local_tls_identity.h"
#include "lvgl_ui.h"
#include "network_identity.h"
#include "nvs_flash.h"
#include "ota_update.h"
#include "sdkconfig.h"
#include "storage.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "serialite";

/* One phone at a time: a second client cannot join and race the operator for the session. */
#define AP_MAX_CLIENTS 1
#define WEB_FQDN CONFIG_SERIALITE_HOSTNAME ".local"
#define MDNS_TTL_SECONDS 120

#if CONFIG_SERIALITE_UART_SHOW_PASSWORD
#define UART_REVEAL true
#else
#define UART_REVEAL false
#endif

static const char *const ALLOWED_HOSTS[] = {WIFI_AP_IP_ADDR, WEB_FQDN};

static app_credentials_t s_credentials;
static char s_fingerprint[HTTPS_FINGERPRINT_TEXT_LEN];
#if CONFIG_SERIALITE_HTTPS
static local_tls_identity_t s_tls_identity;
#endif

static void log_result(const char *name, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s ready", name);
    } else {
        ESP_LOGE(TAG, "%s failed: %s", name, esp_err_to_name(err));
    }
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

#if CONFIG_SERIALITE_HTTPS
static bool hardware_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return true;
}
#endif

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout unusable (%s); erasing it", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    return err;
}

/* ---- local display -------------------------------------------------------- */

static lvgl_ui_credentials_t ui_credentials(void)
{
    return (lvgl_ui_credentials_t){
        .ssid = s_credentials.ssid,
        .password = s_credentials.phrase,
        .web_host = WIFI_AP_IP_ADDR,
        .https_fingerprint = s_fingerprint[0] != '\0' ? s_fingerprint : NULL,
    };
}

static void fill_ui_status(ui_status_format_input_t *out, void *ctx)
{
    (void)ctx;
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const usb_console_status_t usb = usb_console_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    const web_server_status_t web = web_server_get_status();
    *out = (ui_status_format_input_t){
        .ap_started = wifi.started,
        .wifi_clients = wifi.connected_clients,
        .web_clients = web.ws_client_count,
        .web_writer_active = web.ws_client_count > 0 && !web.locked,
        .web_locked = web.locked,
        .usb_connected = usb.connected,
        .usb_rx_bytes = usb.bytes_received,
        .usb_tx_bytes = usb.bytes_sent,
        .bridge_drops = bridge.dropped_from_usb + bridge.dropped_to_usb + bridge.scrollback_dropped_oldest,
    };
}

/* Shows the current credentials to whoever is physically at the device. */
static esp_err_t reveal_credentials(void)
{
    if (lvgl_ui_is_running()) {
        const lvgl_ui_credentials_t ui = ui_credentials();
        return lvgl_ui_set_credentials(&ui, true);
    }
    if (UART_REVEAL) {
        ESP_LOGW(TAG, "WiFi %s password: %s", s_credentials.ssid, s_credentials.phrase);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

/* ---- buttons -------------------------------------------------------------- */

static void on_primary_press(void *ctx)
{
    (void)ctx;
    if (ap_guard_stopped_ap()) {
        /* The password survives a software restart, so this just brings the AP back. */
        esp_restart();
    }
    lvgl_ui_notify_primary_press();
}

static void on_primary_hold(uint32_t held_ms, void *ctx)
{
    (void)ctx;
    lvgl_ui_notify_primary_hold(held_ms, BUTTONS_FACTORY_RESET_HOLD_MS);
}

static void on_factory_reset(void *ctx)
{
    (void)ctx;
    ESP_LOGW(TAG, "factory reset: erasing NVS and forgetting the cached password");
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "factory reset");
    (void)wifi_ap_stop();
    app_credentials_forget_cached();
    const esp_err_t err = storage_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase failed: %s; restarting with a new password anyway", esp_err_to_name(err));
    }
    esp_restart();
}

static void on_secondary_press(void *ctx)
{
    (void)ctx;
    if (web_server_get_status().started) {
        log_result("emergency lock toggle", web_server_toggle_lock());
    }
}

static void on_secondary_hold(void *ctx)
{
    (void)ctx;
    ESP_LOGW(TAG, "power off requested");
    log_result("power off", board_power_off());
}

/* ---- web callbacks (httpd task) ------------------------------------------- */

static esp_err_t rotate_credentials(void *ctx)
{
    (void)ctx;
    if (!lvgl_ui_is_running() && !UART_REVEAL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(app_credentials_rotate(&s_credentials), TAG, "rotation failed");
    const esp_err_t err = reveal_credentials();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "new password stored but not shown yet: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t export_config(char *out, size_t out_size, void *ctx)
{
    (void)ctx;
    return app_credentials_export(&s_credentials, out, out_size);
}

static esp_err_t import_config(const char *json, void *ctx)
{
    (void)ctx;
    return app_credentials_import(&s_credentials, json);
}

/* ---- boot ----------------------------------------------------------------- */

static void advertise_mdns(bool https)
{
    const network_identity_config_t config = {
        .hostname = CONFIG_SERIALITE_HOSTNAME,
        .instance_name = "Serialite",
        .service_type = https ? "_https" : "_http",
        .port = https ? 443 : 80,
        .ttl_seconds = MDNS_TTL_SECONDS,
    };
    const esp_err_t err = network_identity_start(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS unavailable (%s); use %s://" WIFI_AP_IP_ADDR, esp_err_to_name(err), https ? "https" : "http");
    }
}

static void forget_secrets(void)
{
    app_credentials_wipe(&s_credentials);
#if CONFIG_SERIALITE_HTTPS
    local_tls_identity_zeroize(&s_tls_identity);
#endif
}

void app_main(void)
{
    event_log_init();
    const esp_err_t nvs_err = init_nvs();
    log_result("nvs", nvs_err);
    esp_err_t storage_err = nvs_err;
    if (nvs_err == ESP_OK) {
        storage_err = storage_init();
        log_result("storage", storage_err);
    }
    const esp_err_t bridge_err = terminal_bridge_start();
    log_result("terminal_bridge", bridge_err);
    const esp_err_t board_err = board_init();
    log_result(board_get_info()->name, board_err);

    /*
     * Secrets are generated before WiFi is up, when the RNG only has a true
     * entropy source if one is switched on explicitly.
     */
    bootloader_random_enable();
    const esp_err_t cred_err = app_credentials_load(&s_credentials, storage_err == ESP_OK);
    bool tls_ready = false;
#if CONFIG_SERIALITE_HTTPS
    if (cred_err == ESP_OK) {
        const local_tls_identity_result_t tls = local_tls_identity_generate(&s_tls_identity, hardware_random, NULL, WEB_FQDN);
        tls_ready = tls == LOCAL_TLS_IDENTITY_OK;
        if (tls_ready) {
            strlcpy(s_fingerprint, s_tls_identity.fingerprint_text, sizeof(s_fingerprint));
        } else {
            ESP_LOGE(TAG, "TLS identity failed: %s", local_tls_identity_result_name(tls));
        }
    }
#endif
    bootloader_random_disable();
    log_result("credentials", cred_err);

    esp_err_t ui_err = ESP_ERR_NOT_SUPPORTED;
    if (board_err == ESP_OK && board_get_info()->has_display && cred_err == ESP_OK) {
        const lvgl_ui_config_t ui = {
            .credentials = ui_credentials(),
            .status_fn = fill_ui_status,
        };
        ui_err = lvgl_ui_start(&ui);
        log_result("display", ui_err);
    }
    if (board_err == ESP_OK) {
        const buttons_config_t buttons = {
            .on_primary_press = on_primary_press,
            .on_primary_hold = on_primary_hold,
            .on_factory_reset = on_factory_reset,
            .on_secondary_press = on_secondary_press,
            .on_secondary_hold = on_secondary_hold,
        };
        log_result("buttons", buttons_start(&buttons));
    }
    /* The serial bridge is useful on its own, so it starts whatever happens to WiFi. */
    log_result("usb_console", usb_console_start());

    if (cred_err != ESP_OK || !credential_boot_policy_can_start_ap(s_credentials.source, ui_err == ESP_OK, UART_REVEAL)) {
        ESP_LOGE(TAG, "AP not started: %s", cred_err != ESP_OK ? "no credentials" : "a new password cannot be shown on this board");
        forget_secrets();
        return;
    }
    if (ui_err != ESP_OK && UART_REVEAL) {
        (void)reveal_credentials();
    }
    if (tls_ready && ui_err != ESP_OK && !UART_REVEAL) {
        ESP_LOGW(TAG, "HTTPS needs its fingerprint on screen to be verifiable; serving HTTP");
        tls_ready = false;
        s_fingerprint[0] = '\0';
    }

    wifi_ap_settings_t ap;
    esp_err_t wifi_err = app_credentials_ap_config(&s_credentials, AP_MAX_CLIENTS, &ap);
    if (wifi_err == ESP_OK) {
        wifi_err = wifi_ap_start(&ap);
    }
    memset(&ap, 0, sizeof(ap));
    log_result("wifi_ap", wifi_err);

    esp_err_t web_err = ESP_ERR_INVALID_STATE;
    if (wifi_err == ESP_OK) {
        const web_server_config_t web = {
#if CONFIG_SERIALITE_HTTPS
            .tls_identity = tls_ready ? &s_tls_identity : NULL,
#endif
            .allowed_hosts = ALLOWED_HOSTS,
            .allowed_host_count = sizeof(ALLOWED_HOSTS) / sizeof(ALLOWED_HOSTS[0]),
            .rotate_credentials = rotate_credentials,
            .export_config = export_config,
            .import_config = import_config,
        };
        web_err = web_server_start(&web);
        log_result("web_server", web_err);
        if (web_err == ESP_OK) {
            advertise_mdns(tls_ready);
        }
        log_result("ap_guard", ap_guard_start());
    }
#if CONFIG_SERIALITE_HTTPS
    /* esp_https_server keeps its own copies of the certificate and key. */
    local_tls_identity_zeroize(&s_tls_identity);
#endif

    /* An OTA image only becomes permanent once every way to update it again works. */
    if (bridge_err == ESP_OK && wifi_err == ESP_OK && web_err == ESP_OK) {
        log_result("ota_valid", ota_update_mark_running_app_valid());
    } else {
        ESP_LOGW(TAG, "running image not confirmed: a pending OTA rollback stays armed");
    }
}
