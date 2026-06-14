#include "web_server.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "usb_console.h"
#include "wifi_ap.h"

static const char *TAG = "web_server";

static esp_err_t index_handler(httpd_req_t *req)
{
    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();

    char body[512];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM</title></head><body>"
        "<h1>ESP32-KVM</h1>"
        "<p>WiFi AP: %s</p>"
        "<p>IP: %s</p>"
        "<p>Clientes: %u</p>"
        "<p>USB: %s</p>"
        "<p>RX bytes: %llu</p>"
        "<p>TX bytes: %llu</p>"
        "<p>Terminal WebSocket: pendiente</p>"
        "</body></html>",
        wifi.started ? "activo" : "inactivo",
        wifi.ip_addr,
        wifi.connected_clients,
        usb.connected ? "conectado" : "desconectado",
        (unsigned long long)usb.bytes_received,
        (unsigned long long)usb.bytes_sent);

    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_server_start(void)
{
    if (!wifi_ap_get_status().started) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start failed");

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };

    const esp_err_t err = httpd_register_uri_handler(server, &index_uri);
    if (err != ESP_OK) {
        httpd_stop(server);
        ESP_LOGE(TAG, "index handler failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
