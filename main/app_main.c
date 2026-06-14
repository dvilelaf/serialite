#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "lvgl_ui.h"
#include "storage.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "esp32_kvm";

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

static esp_err_t generate_ephemeral_wifi_config(kvm_wifi_ap_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read softap mac failed");

    uint8_t random_bytes[12] = {0};
    esp_fill_random(random_bytes, sizeof(random_bytes));

    const int ssid_len = snprintf(
        config->ssid,
        sizeof(config->ssid),
        "ESP32-KVM-%02X%02X%02X",
        mac[3],
        mac[4],
        mac[5]);
    if (ssid_len < 0 || ssid_len >= (int)sizeof(config->ssid)) {
        return ESP_ERR_INVALID_SIZE;
    }

    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        config->password[i] = alphabet[random_bytes[i] % (sizeof(alphabet) - 1)];
    }
    config->password[sizeof(random_bytes)] = '\0';
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
    log_init_result("storage", storage_init());
    log_init_result("terminal_bridge", terminal_bridge_start());
    ESP_ERROR_CHECK(storage_load_config(&config));

    kvm_wifi_ap_config_t mapped_wifi_config = {
        .channel = config.wifi.channel,
        .max_clients = config.wifi.max_clients,
    };
    strlcpy(mapped_wifi_config.ssid, config.wifi.ssid, sizeof(mapped_wifi_config.ssid));
    strlcpy(mapped_wifi_config.password, config.wifi.password, sizeof(mapped_wifi_config.password));

    bool ephemeral_credentials = false;
    if (nvs_recovered || !storage_wifi_config_is_valid(&config.wifi)) {
        ESP_ERROR_CHECK(generate_ephemeral_wifi_config(&mapped_wifi_config));
        ephemeral_credentials = true;
    }

    const lvgl_ui_boot_status_t ui_status = {
        .ssid = mapped_wifi_config.ssid,
        .password = mapped_wifi_config.password,
        .ip_addr = "192.168.4.1",
        .usb_connected = false,
    };
    const esp_err_t ui_err = lvgl_ui_start(&ui_status);
    log_init_result("lvgl_ui", ui_err);
    if (ui_err != ESP_OK && ephemeral_credentials) {
        ESP_LOGE(TAG, "AP skipped: ephemeral password cannot be safely exposed without local display");
        log_init_result("usb_console", usb_console_start());
        return;
    }

    esp_err_t wifi_err = wifi_ap_start(&mapped_wifi_config);
    log_init_result("wifi_ap", wifi_err);
    log_init_result("usb_console", usb_console_start());
    if (wifi_err == ESP_OK) {
        log_init_result("web_server", web_server_start());
    } else {
        ESP_LOGE(TAG, "web_server skipped because AP did not start");
    }
}
