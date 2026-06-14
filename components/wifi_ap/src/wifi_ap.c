#include "wifi_ap.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"

static const char *TAG = "wifi_ap";

static wifi_ap_status_t s_status = {
    .ip_addr = "192.168.4.1",
    .connected_clients = 0,
    .started = false,
};

static bool wifi_ap_config_is_valid(const kvm_wifi_ap_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    const size_t ssid_len = strnlen(config->ssid, WIFI_AP_SSID_MAX_LEN);
    const size_t password_len = strnlen(config->password, WIFI_AP_PASSWORD_MAX_LEN);

    if (ssid_len == 0 || ssid_len > WIFI_AP_SSID_MAX_BYTES) {
        return false;
    }
    if (password_len < 8 || password_len > WIFI_AP_PASSWORD_MAX_BYTES) {
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

static void wifi_ap_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        s_status.connected_clients++;
        ESP_LOGI(TAG, "client connected, clients=%u", s_status.connected_clients);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED && s_status.connected_clients > 0) {
        s_status.connected_clients--;
        ESP_LOGI(TAG, "client disconnected, clients=%u", s_status.connected_clients);
    }

    (void)event_data;
}

esp_err_t wifi_ap_start(const kvm_wifi_ap_config_t *config)
{
    if (!wifi_ap_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        esp_netif_destroy_default_wifi(ap_netif);
        return err;
    }

    esp_event_handler_instance_t wifi_event_instance = NULL;
    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_ap_event_handler,
        NULL,
        &wifi_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi event handler failed: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(ap_netif);
        return err;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, config->ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, config->password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(config->ssid);
    wifi_config.ap.channel = config->channel;
    wifi_config.ap.max_connection = config->max_clients;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = true;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AP startup failed: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_instance);
        esp_wifi_clear_default_wifi_driver_and_handlers(ap_netif);
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(ap_netif);
        return err;
    }

    s_status.started = true;
    ESP_LOGI(TAG, "AP started: ssid=%s ip=%s", config->ssid, s_status.ip_addr);
    return ESP_OK;
}

wifi_ap_status_t wifi_ap_get_status(void)
{
    return s_status;
}
