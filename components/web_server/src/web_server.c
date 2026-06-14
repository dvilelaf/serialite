#include "web_server.h"

#include <stdio.h>

#include "ap_exposure_policy.h"
#include "app_watchdog.h"
#include "demo_serial_runtime.h"
#include "diagnostics_export.h"
#include "event_log.h"
#include "credentials.h"
#include "http_rate_limit.h"
#include "http_route_policy.h"
#include "https_fingerprint.h"
#include "local_pairing.h"
#include "ota_update.h"
#include "ota_update_policy.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_input_policy.h"
#include "web_demo_policy.h"
#include "web_macro_policy.h"
#include "web_security.h"
#include "web_terminal_ansi.h"
#include "web_terminal_contract.h"
#include "wifi_ap.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web_server";

#define WEB_MAX_WS_CLIENTS 4
#define WEB_TX_QUEUE_DEPTH 16
#define WEB_TX_CHUNK_MAX 256
#define WEB_SCROLLBACK_SEND_CHUNK_MAX 256
#define WEB_REQUEST_BODY_MAX HTTP_ROUTE_CONFIG_IMPORT_BODY_MAX
#define WEB_HEADER_VALUE_MAX 160
#define WEB_DIAG_EVENT_LIMIT 16
#define WEB_PASTE_CONFIRM_BYTES 64
#define WEB_PASTE_MAX_BYTES 2048
#define WEB_AP_GUARD_TASK_STACK 3072
#define WEB_AP_GUARD_TASK_PRIORITY 2
#define WEB_AP_GUARD_INTERVAL_MS 30000
#define WEB_OTA_RECV_CHUNK 2048

typedef struct {
    size_t len;
    uint8_t data[WEB_TX_CHUNK_MAX];
} web_tx_chunk_t;

static httpd_handle_t s_server;
static bool s_server_tls;
static int s_ws_fds[WEB_MAX_WS_CLIENTS];
static QueueHandle_t s_tx_queue;
static TaskHandle_t s_web_tx_task;
static TaskHandle_t s_ap_guard_task;
static web_terminal_ansi_state_t s_web_ansi_state;
static web_security_state_t s_security;
static web_input_policy_state_t s_input_policy;
static ap_exposure_policy_state_t s_ap_exposure_policy;
static http_rate_limit_state_t s_http_rate_limit;
static SemaphoreHandle_t s_http_rate_limit_lock;
static StaticSemaphore_t s_http_rate_limit_lock_storage;
static SemaphoreHandle_t s_ws_fds_lock;
static StaticSemaphore_t s_ws_fds_lock_storage;
static SemaphoreHandle_t s_demo_writer_lock;
static StaticSemaphore_t s_demo_writer_lock_storage;
static portMUX_TYPE s_runtime_status_lock = portMUX_INITIALIZER_UNLOCKED;
static web_server_status_t s_runtime_status;
static web_server_rotate_credentials_fn_t s_rotate_credentials;
static void *s_rotate_credentials_ctx;
static web_server_pairing_event_fn_t s_pairing_event;
static void *s_pairing_event_ctx;
static web_server_export_config_fn_t s_export_config;
static web_server_import_config_fn_t s_import_config;
static void *s_config_ctx;
static bool s_credential_reboot_pending;
static bool s_macros_enabled;
static local_pairing_state_t s_pairing;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static bool security_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return true;
}

static void runtime_status_set_started(bool started)
{
    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status.started = started;
    if (!started) {
        s_runtime_status.tls_active = false;
    }
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static void runtime_status_set_tls_active(bool active)
{
    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status.tls_active = active;
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static void runtime_status_set_writer_active(bool active)
{
    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status.writer_active = active;
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static void runtime_status_set_locked(bool locked)
{
    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status.locked = locked;
    if (locked) {
        s_runtime_status.writer_active = false;
    }
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static void secure_zero(void *ptr, size_t len)
{
    if (ptr == NULL) {
        return;
    }

    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static void notify_pairing_event(web_server_pairing_event_t event)
{
    if (s_pairing_event != NULL) {
        s_pairing_event(event, s_pairing_event_ctx);
    }
}

static void send_no_store_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; connect-src 'self' ws:; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
}

static esp_err_t enforce_http_rate_limit(httpd_req_t *req)
{
    if (s_http_rate_limit_lock == NULL) {
        return ESP_OK;
    }

    xSemaphoreTake(s_http_rate_limit_lock, portMAX_DELAY);
    const http_rate_limit_result_t result = http_rate_limit_evaluate(&s_http_rate_limit, now_ms());
    xSemaphoreGive(s_http_rate_limit_lock);

    if (result == HTTP_RATE_LIMIT_ACCEPT) {
        return ESP_OK;
    }

    event_log_append(EVENT_LOG_SECURITY, now_ms(), "http rate limit");
    send_no_store_headers(req);
    httpd_resp_set_status(req, "429 Too Many Requests");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "rate limit", HTTPD_RESP_USE_STRLEN);
}

static http_route_method_t route_method_from_httpd(httpd_method_t method)
{
    switch (method) {
        case HTTP_GET:
            return HTTP_ROUTE_METHOD_GET;
        case HTTP_POST:
            return HTTP_ROUTE_METHOD_POST;
        default:
            return HTTP_ROUTE_METHOD_OTHER;
    }
}

static esp_err_t send_route_policy_error(httpd_req_t *req, http_route_policy_result_t result)
{
    event_log_append(EVENT_LOG_SECURITY, now_ms(), http_route_policy_result_name(result));
    switch (result) {
        case HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE:
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "request body too large");
        case HTTP_ROUTE_POLICY_REJECT_METHOD:
            return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "method not allowed");
        case HTTP_ROUTE_POLICY_REJECT_NOT_FOUND:
            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        case HTTP_ROUTE_POLICY_ALLOW:
        default:
            return ESP_OK;
    }
}

static esp_err_t validate_route_policy(httpd_req_t *req)
{
    const http_route_policy_result_t result = http_route_policy_allowed(
        req->uri,
        route_method_from_httpd(req->method),
        req->content_len);
    return result == HTTP_ROUTE_POLICY_ALLOW ? ESP_OK : send_route_policy_error(req, result);
}

static esp_err_t validate_same_origin_header(httpd_req_t *req, const char *action)
{
    char origin[WEB_HEADER_VALUE_MAX];
    char host[WEB_HEADER_VALUE_MAX];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        !web_security_origin_allowed(origin, host)) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), action);
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid origin");
    }
    return ESP_OK;
}

static bool extract_session_cookie(httpd_req_t *req, char out_token[WEB_SECURITY_TOKEN_BUF_LEN])
{
    char cookie[WEB_HEADER_VALUE_MAX];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }

    const char *name = "kvm_session=";
    char *start = strstr(cookie, name);
    if (start == NULL) {
        return false;
    }
    start += strlen(name);
    size_t len = 0;
    while (start[len] != '\0' && start[len] != ';' && len < WEB_SECURITY_TOKEN_LEN) {
        len++;
    }
    if (len != WEB_SECURITY_TOKEN_LEN) {
        return false;
    }
    memcpy(out_token, start, len);
    out_token[len] = '\0';
    return true;
}

static bool request_authenticated(httpd_req_t *req, char out_token[WEB_SECURITY_TOKEN_BUF_LEN])
{
    return extract_session_cookie(req, out_token) &&
           web_security_session_valid(&s_security, out_token, now_ms());
}

static esp_err_t redirect_to(httpd_req_t *req, const char *location)
{
    send_no_store_headers(req);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    return httpd_resp_send(req, "", 0);
}

static esp_err_t require_auth_or_redirect(httpd_req_t *req, char out_token[WEB_SECURITY_TOKEN_BUF_LEN])
{
    if (request_authenticated(req, out_token)) {
        return ESP_OK;
    }
    return redirect_to(req, "/login");
}

static bool url_decode(char *value)
{
    char *write = value;
    for (char *read = value; *read != '\0'; ++read) {
        if (*read == '+') {
            *write++ = ' ';
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            char hex[3] = {read[1], read[2], '\0'};
            char *end = NULL;
            const long decoded = strtol(hex, &end, 16);
            if (end == NULL || *end != '\0' || decoded < 0 || decoded > 255) {
                return false;
            }
            *write++ = (char)decoded;
            read += 2;
        } else {
            *write++ = *read;
        }
    }
    *write = '\0';
    return true;
}

static bool form_value(const char *body, const char *key, char *out, size_t out_size)
{
    if (body == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const size_t key_len = strlen(key);
    const char *cursor = body;
    while (cursor != NULL && *cursor != '\0') {
        if (strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
            const char *value = cursor + key_len + 1;
            size_t len = 0;
            while (value[len] != '\0' && value[len] != '&' && len < out_size - 1) {
                len++;
            }
            out[len] = '\0';
            memcpy(out, value, len);
            out[len] = '\0';
            return url_decode(out);
        }
        cursor = strchr(cursor, '&');
        if (cursor != NULL) {
            cursor++;
        }
    }
    return false;
}

static esp_err_t read_small_body(httpd_req_t *req, char *out, size_t out_size)
{
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route policy rejected");
    if (req->content_len >= out_size || req->content_len > WEB_REQUEST_BODY_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "request body too large");
    }

    size_t received = 0;
    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, out + received, req->content_len - received);
        if (ret <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "request body read failed");
        }
        received += (size_t)ret;
    }
    out[received] = '\0';
    return ESP_OK;
}

static bool csrf_header_valid(httpd_req_t *req, const char *session_token)
{
    char csrf[WEB_HEADER_VALUE_MAX];
    if (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", csrf, sizeof(csrf)) != ESP_OK) {
        return false;
    }
    return web_security_csrf_valid(&s_security, session_token, csrf, now_ms());
}

static esp_err_t require_mutating_auth_csrf_origin(
    httpd_req_t *req,
    const char *origin_action,
    const char *csrf_action,
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN])
{
    ESP_RETURN_ON_ERROR(validate_same_origin_header(req, origin_action), TAG, "origin rejected");
    if (!request_authenticated(req, session_token)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "auth required");
    }
    if (!csrf_header_valid(req, session_token)) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), csrf_action);
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid csrf");
    }
    return ESP_OK;
}

static const char *writer_state_name(web_security_writer_state_t state)
{
    switch (state) {
        case WEB_SECURITY_WRITER_READ_ONLY:
            return "read-only";
        case WEB_SECURITY_WRITER_ACTIVE:
            return "write-active";
        case WEB_SECURITY_WRITER_BUSY:
            return "writer-busy";
        case WEB_SECURITY_WRITER_INVALID_SESSION:
        default:
            return "locked";
    }
}

static const char *level_name(event_log_level_t level)
{
    switch (level) {
        case EVENT_LOG_INFO:
            return "info";
        case EVENT_LOG_WARN:
            return "warn";
        case EVENT_LOG_ERROR:
            return "error";
        case EVENT_LOG_SECURITY:
            return "security";
        default:
            return "unknown";
    }
}

static void remove_ws_fd(int fd)
{
    if (s_ws_fds_lock != NULL) {
        xSemaphoreTake(s_ws_fds_lock, portMAX_DELAY);
    }
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
        }
    }
    if (s_ws_fds_lock != NULL) {
        xSemaphoreGive(s_ws_fds_lock);
    }
}

static esp_err_t add_ws_fd(int fd)
{
    if (s_ws_fds_lock != NULL) {
        xSemaphoreTake(s_ws_fds_lock, portMAX_DELAY);
    }
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] == fd) {
            if (s_ws_fds_lock != NULL) {
                xSemaphoreGive(s_ws_fds_lock);
            }
            return ESP_OK;
        }
    }
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] < 0) {
            s_ws_fds[i] = fd;
            if (s_ws_fds_lock != NULL) {
                xSemaphoreGive(s_ws_fds_lock);
            }
            return ESP_OK;
        }
    }
    if (s_ws_fds_lock != NULL) {
        xSemaphoreGive(s_ws_fds_lock);
    }
    return ESP_ERR_NO_MEM;
}

static void snapshot_ws_fds(int fds[WEB_MAX_WS_CLIENTS])
{
    if (s_ws_fds_lock != NULL) {
        xSemaphoreTake(s_ws_fds_lock, portMAX_DELAY);
    }
    memcpy(fds, s_ws_fds, sizeof(s_ws_fds));
    if (s_ws_fds_lock != NULL) {
        xSemaphoreGive(s_ws_fds_lock);
    }
}

static void close_all_ws_clients(void)
{
    if (s_server == NULL) {
        return;
    }

    int fds[WEB_MAX_WS_CLIENTS];
    snapshot_ws_fds(fds);
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (fds[i] >= 0) {
            httpd_sess_trigger_close(s_server, fds[i]);
            remove_ws_fd(fds[i]);
        }
    }
}

static void emergency_lock_work(void *arg)
{
    (void)arg;
    web_security_invalidate_all(&s_security);
    runtime_status_set_locked(true);
    close_all_ws_clients();
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "emergency lock engaged");
}

static void web_tx_task(void *arg)
{
    (void)arg;
    web_tx_chunk_t chunk;
    while (true) {
        if (xQueueReceive(s_tx_queue, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = chunk.data,
            .len = chunk.len,
        };

        int fds[WEB_MAX_WS_CLIENTS];
        snapshot_ws_fds(fds);

        for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
            if (fds[i] >= 0) {
                esp_err_t err = httpd_ws_send_frame_async(s_server, fds[i], &frame);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "dropping websocket client fd=%d: %s", fds[i], esp_err_to_name(err));
                    event_log_append(EVENT_LOG_WARN, now_ms(), "websocket client dropped during broadcast");
                    remove_ws_fd(fds[i]);
                }
            }
        }
    }
}

static void ap_guard_task(void *arg)
{
    (void)arg;
    // This guard intentionally sleeps longer than the task watchdog timeout.
    // Registering it would create a false-positive reboot loop while the AP is healthy.
    while (true) {
        const wifi_ap_status_t wifi = wifi_ap_get_status();
        const terminal_bridge_status_t bridge = terminal_bridge_get_status();
        const ap_exposure_policy_result_t result = ap_exposure_policy_evaluate(
            &s_ap_exposure_policy,
            wifi.started,
            wifi.connected_clients,
            bridge.subscriber_count,
            now_ms());
        if (result == AP_EXPOSURE_STOP_AP) {
            event_log_append(EVENT_LOG_SECURITY, now_ms(), "AP idle timeout; stopping AP");
            const esp_err_t err = wifi_ap_stop();
            runtime_status_set_started(false);
            runtime_status_set_writer_active(false);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "AP idle timeout stop failed: %s; keeping firmware alive", esp_err_to_name(err));
                event_log_append(EVENT_LOG_ERROR, now_ms(), "AP idle timeout stop failed; firmware kept alive");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WEB_AP_GUARD_INTERVAL_MS));
    }
}

static void bridge_output_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (s_tx_queue == NULL || data == NULL || len == 0) {
        return;
    }

    size_t offset = 0;
    while (offset < len) {
        web_tx_chunk_t chunk = {0};
        const size_t input_len = (len - offset) > WEB_TX_CHUNK_MAX ? WEB_TX_CHUNK_MAX : (len - offset);
        chunk.len = web_terminal_ansi_filter(&s_web_ansi_state, data + offset, input_len, chunk.data, sizeof(chunk.data));
        offset += input_len;
        if (chunk.len == 0) {
            continue;
        }
        if (xQueueSend(s_tx_queue, &chunk, 0) != pdTRUE) {
            ESP_LOGW(TAG, "websocket tx queue full; dropped %u bytes", (unsigned)(len - offset));
            event_log_append(EVENT_LOG_WARN, now_ms(), "websocket tx queue full");
            return;
        }
    }
}

static void cleanup_failed_start(httpd_handle_t server)
{
    if (s_web_tx_task != NULL) {
        vTaskDelete(s_web_tx_task);
        s_web_tx_task = NULL;
    }
    if (s_ap_guard_task != NULL) {
        vTaskDelete(s_ap_guard_task);
        s_ap_guard_task = NULL;
    }
    if (s_tx_queue != NULL) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    if (server != NULL) {
        if (s_server_tls) {
            (void)httpd_ssl_stop(server);
        } else {
            (void)httpd_stop(server);
        }
    }
    s_server = NULL;
    s_server_tls = false;
    runtime_status_set_started(false);
    runtime_status_set_writer_active(false);
}

static void send_scrollback_to_ws_client(int fd)
{
    uint8_t snapshot[1024];
    const size_t len = terminal_bridge_snapshot_recent_output(snapshot, sizeof(snapshot));
    if (len == 0) {
        return;
    }
    uint8_t filtered[1024];
    web_terminal_ansi_state_t ansi_state;
    web_terminal_ansi_init(&ansi_state);
    const size_t filtered_len = web_terminal_ansi_filter(&ansi_state, snapshot, len, filtered, sizeof(filtered));
    if (filtered_len == 0) {
        return;
    }

    size_t offset = 0;
    while (offset < filtered_len) {
        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = filtered + offset,
            .len = filtered_len - offset,
        };
        if (frame.len > WEB_SCROLLBACK_SEND_CHUNK_MAX) {
            frame.len = WEB_SCROLLBACK_SEND_CHUNK_MAX;
        }

        const esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "scrollback send failed fd=%d: %s", fd, esp_err_to_name(err));
            event_log_append(EVENT_LOG_WARN, now_ms(), "scrollback send failed");
            remove_ws_fd(fd);
            return;
        }
        offset += frame.len;
    }
}

static esp_err_t index_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");

    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    const demo_serial_runtime_status_t demo = demo_serial_runtime_get_status();

    char body[1600];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM</title><style>body{background:#050b09;color:#e9fff8;font:16px sans-serif;margin:24px}"
        "a,button{color:#7dffe1}.card{border:1px solid #164235;border-radius:16px;padding:16px;max-width:720px}</style></head><body>"
        "<div class=\"card\">"
        "<h1>ESP32-KVM</h1>"
        "<p><strong>Serial rescue console. Not HDMI KVM.</strong></p>"
        "<p>WiFi AP: %s</p>"
        "<p>IP: %s</p>"
        "<p>Clientes: %u</p>"
        "<p>USB: %s</p>"
        "<p>Demo serial: %s, bytes=%llu</p>"
        "<p>RX bytes: %llu</p>"
        "<p>TX bytes: %llu</p>"
        "<p>Bridge USB RX: %llu</p>"
        "<p>Bridge USB TX: %llu</p>"
        "<p>Scrollback: %u/%u bytes, dropped old=%llu</p>"
        "<p><a href=\"/terminal\">Abrir terminal</a></p>"
        "<p><a href=\"/diagnostics\">Diagnostico</a> | <a href=\"/runbook\">Runbook</a> | <a href=\"/macros\">Macros</a> | <a href=\"/credentials\">Credentials</a> | <a href=\"/config\">Config</a> | <a href=\"/ota\">Firmware</a> | <a href=\"/about\">About</a></p>"
        "<p><a href=\"/logout\" onclick=\"event.preventDefault();fetch('/logout',{method:'POST',headers:{'X-CSRF-Token':'%s'}}).then(()=>location='/login')\">Logout</a></p>"
        "</div>"
        "</body></html>",
        wifi.started ? "activo" : "inactivo",
        wifi.ip_addr,
        wifi.connected_clients,
        usb.connected ? "conectado" : "desconectado",
        demo.active ? "activo" : "inactivo",
        (unsigned long long)demo.bytes_emitted,
        (unsigned long long)usb.bytes_received,
        (unsigned long long)usb.bytes_sent,
        (unsigned long long)bridge.bytes_from_usb,
        (unsigned long long)bridge.bytes_to_usb,
        (unsigned)bridge.scrollback_retained,
        (unsigned)bridge.scrollback_capacity,
        (unsigned long long)bridge.scrollback_dropped_oldest,
        web_security_csrf_for_session(&s_security, session_token, now_ms()));

    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t about_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");

    const esp_app_desc_t *app = esp_app_get_description();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    const bool https_enabled = s_server_tls;

    char body[2300];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM About</title><style>body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px}"
        ".card{border:1px solid #174436;border-radius:16px;padding:14px;background:#030807;max-width:760px}"
        "dt{color:#7dffe1}dd{margin:0 0 8px 0}a{color:#7dffe1}</style></head><body><main class=\"card\">"
        "<h1>About ESP32-KVM</h1><p><a href=\"/\">Status</a> | <a href=\"/terminal\">Terminal</a> | <a href=\"/diagnostics\">Diagnostics</a> | <a href=\"/runbook\">Runbook</a> | <a href=\"/macros\">Macros</a> | <a href=\"/credentials\">Credentials</a> | <a href=\"/ota\">Firmware</a></p>"
        "<p>Serial rescue console over local WiFi AP. It is not HDMI KVM, HID remote input, virtual media, power control, cloud access, or command automation.</p>"
        "<dl><dt>Firmware</dt><dd>%s</dd><dt>Project</dt><dd>%s</dd><dt>Build date</dt><dd>%s %s</dd>"
        "<dt>IDF</dt><dd>%s</dd><dt>Security model</dt><dd>Local AP, web auth, CSRF, Origin checks, single writer, RAM diagnostics.</dd>"
        "<dt>HTTPS fingerprint mode</dt><dd>%s. See docs/https-fingerprint.md; operators must compare the browser certificate SHA-256 fingerprint with the AMOLED fingerprint before trusting TLS.</dd>"
        "<dt>Production build</dt><dd>Requires Secure Boot, Flash Encryption, encrypted NVS for persisted secrets, and closed debug/JTAG. Treat unsigned debug builds as lab-only.</dd>"
        "<dt>Scrollback</dt><dd>%u/%u bytes retained, %llu old bytes dropped.</dd>"
        "<dt>Emergency lock</dt><dd>Hold BOOT for 3 seconds to invalidate web sessions, release write control, and close WebSockets.</dd>"
        "<dt>Factory reset</dt><dd>Hold BOOT for 10 seconds. This clears project NVS config and reboots.</dd></dl>"
        "</main></body></html>",
        app != NULL ? app->version : "unknown",
        app != NULL ? app->project_name : "esp32-kvm",
        app != NULL ? app->date : "unknown",
        app != NULL ? app->time : "",
        esp_get_idf_version(),
        https_enabled ? "enabled" : "disabled by default",
        (unsigned)bridge.scrollback_retained,
        (unsigned)bridge.scrollback_capacity,
        (unsigned long long)bridge.scrollback_dropped_oldest);
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "about overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t runbook_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");

    static const char body[] =
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Runbook</title><style>"
        "body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px;line-height:1.45}"
        "main{border:1px solid #174436;border-radius:16px;padding:16px;background:#030807;max-width:820px}"
        "h2{color:#7dffe1}a{color:#7dffe1}li{margin:8px 0}code{color:#bffff0}</style></head><body><main>"
        "<h1>Runbook de rescate</h1>"
        "<p><a href=\"/\">Status</a> | <a href=\"/terminal\">Terminal</a> | <a href=\"/diagnostics\">Diagnostics</a> | <a href=\"/credentials\">Credentials</a> | <a href=\"/ota\">Firmware</a> | <a href=\"/about\">About</a></p>"
        "<h2>Antes de escribir</h2><ol>"
        "<li>Confirma que estas conectado al AP local correcto y fisicamente junto al servidor.</li>"
        "<li>Comprueba en Status que USB esta conectado y que no hay drops creciendo rapidamente.</li>"
        "<li>Abre Terminal en modo solo lectura y espera salida antes de pedir escritura.</li>"
        "</ol><h2>Recuperar acceso</h2><ol>"
        "<li>Pulsa <code>Request write control</code> solo si necesitas enviar comandos.</li>"
        "<li>Usa comandos de diagnostico de bajo riesgo primero: <code>ip addr</code>, <code>ip route</code>, <code>systemctl status</code>, <code>journalctl -xb</code>.</li>"
        "<li>Evita pegar bloques largos. El paste grande se confirma y se trocea, pero sigue ejecutandose en la consola fisica.</li>"
        "<li>Libera escritura con <code>Release</code> al terminar.</li>"
        "</ol><h2>Si algo falla</h2><ol>"
        "<li>Si USB aparece desconectado, revisa cable, puerto y que el servidor exponga consola CDC ACM.</li>"
        "<li>Si hay rate limit, espera unos segundos y reduce recargas/conexiones.</li>"
        "<li>Si pierdes control operacional de la terminal web, manten BOOT 3s para invalidar sesiones y cerrar WebSockets.</li>"
        "<li>Si las credenciales son desconocidas, pulsa BOOT para revelar temporales o manten BOOT 10s para factory reset.</li>"
        "<li>Usa Diagnostics para copiar contadores y eventos sin exponer passwords.</li>"
        "</ol><h2>Limites</h2><p>Esto es consola serie local. No hay HDMI, HID, virtual media, power cycle ni acceso cloud.</p>"
        "</main></body></html>";

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t macros_page_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    const char *csrf = web_security_csrf_for_session(&s_security, session_token, now_ms());
    if (csrf == NULL) {
        return redirect_to(req, "/login");
    }

    const web_macro_descriptor_t *macros = NULL;
    const size_t macro_count = web_macro_policy_list(&macros);
    const usb_console_status_t usb = usb_console_get_status();
    const demo_serial_runtime_status_t demo = demo_serial_runtime_get_status();
    const web_security_writer_state_t writer = web_security_writer_state(&s_security, session_token, now_ms());
    const bool can_run_state = s_macros_enabled && writer == WEB_SECURITY_WRITER_ACTIVE && usb.connected && !demo.active;

    char body[4200];
    int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Macros</title><style>"
        "body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px;line-height:1.45}"
        "main{border:1px solid #174436;border-radius:16px;padding:16px;background:#030807;max-width:820px}"
        "button{border:1px solid #1f5b4b;border-radius:12px;background:#102a23;color:#e9fff8;padding:10px 12px;font:inherit;font-weight:700}"
        "button:disabled{opacity:.45}code{color:#bffff0}a{color:#7dffe1}.macro{border-top:1px solid #174436;padding:12px 0}</style></head><body><main>"
        "<h1>Safe macros</h1><p><a href=\"/terminal\">Terminal</a> | <a href=\"/\">Status</a> | <a href=\"/runbook\">Runbook</a></p>"
        "<p>Macros are visible for audit but disabled by default. They never run automatically and require explicit confirmation, USB connected, demo off, and active write control.</p>"
        "<p>Status: macros=%s, USB=%s, demo=%s, writer=%s.</p>",
        s_macros_enabled ? "enabled" : "disabled",
        usb.connected ? "connected" : "disconnected",
        demo.active ? "active" : "inactive",
        writer_state_name(writer));
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "macros overflow");
    }

    for (size_t i = 0; i < macro_count; ++i) {
        const int remaining = (int)sizeof(body) - written;
        if (remaining <= 1) {
            break;
        }
        const int add = snprintf(
            body + written,
            (size_t)remaining,
            "<section class=\"macro\"><h2>%s</h2><p><code>%s</code></p>"
            "<button %s onclick=\"runMacro('%s')\">Run with confirmation</button></section>",
            macros[i].label,
            macros[i].command,
            can_run_state ? "" : "disabled",
            macros[i].id);
        if (add < 0 || add >= remaining) {
            break;
        }
        written += add;
    }

    const int remaining = (int)sizeof(body) - written;
    const int tail = snprintf(
        body + written,
        (size_t)remaining,
        "<script>const CSRF='%s';async function runMacro(id){if(!confirm('Send macro '+id+' to the physical console?'))return;"
        "const body='id='+encodeURIComponent(id)+'&confirm=yes';const r=await fetch('/api/macros/run',{method:'POST',headers:{'X-CSRF-Token':CSRF,'Content-Type':'application/x-www-form-urlencoded'},body});alert(r.status+' '+await r.text())}</script>"
        "</main></body></html>",
        csrf);
    if (tail < 0 || tail >= remaining) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "macros overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t login_get_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    if (request_authenticated(req, (char[WEB_SECURITY_TOKEN_BUF_LEN]){0})) {
        return redirect_to(req, "/");
    }

    static const char login_page[] =
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Login</title><style>"
        "body{margin:0;min-height:100vh;background:linear-gradient(180deg,#020504,#07130f);color:#eafff8;font:16px sans-serif;display:grid;place-items:center}"
        "main{width:min(420px,calc(100% - 32px));border:1px solid #174436;border-radius:22px;padding:24px;background:#030807}"
        "input,button{width:100%;box-sizing:border-box;border-radius:12px;padding:13px;margin-top:12px;font:inherit}"
        "input{background:#000;color:#fff;border:1px solid #245c4c}button{background:#0c3429;color:#bffff0;border:1px solid #2ee6b8}"
        "p{color:#8bb5aa}</style></head><body><main><h1>KVM</h1>"
        "<p>Serial rescue console. Press BOOT on the device to reveal the first-login pair code.</p>"
        "<form method=\"post\" action=\"/login\"><input name=\"password\" type=\"password\" autocomplete=\"current-password\" placeholder=\"Web password\" autofocus>"
        "<input name=\"pair\" inputmode=\"numeric\" pattern=\"[0-9]{6}\" autocomplete=\"one-time-code\" placeholder=\"First-login pair code\">"
        "<button type=\"submit\">Unlock web console</button></form></main></body></html>";

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, login_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t login_post_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char body[WEB_REQUEST_BODY_MAX + 1];
    esp_err_t err = read_small_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        secure_zero(body, sizeof(body));
        return err;
    }

    char password[WEB_SECURITY_PASSWORD_MAX_LEN];
    if (!form_value(body, "password", password, sizeof(password))) {
        secure_zero(body, sizeof(body));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing password");
    }
    char pair_code[LOCAL_PAIRING_CODE_BUF_LEN] = {0};
    const bool pairing_pending = local_pairing_required(&s_pairing);
    if (pairing_pending && !form_value(body, "pair", pair_code, sizeof(pair_code))) {
        secure_zero(body, sizeof(body));
        secure_zero(password, sizeof(password));
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "login rejected: missing pair code");
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid credentials");
    }
    secure_zero(body, sizeof(body));

    const web_security_login_result_t result = web_security_login(
        &s_security,
        password,
        now_ms(),
        security_random,
        NULL);
    secure_zero(password, sizeof(password));

    if (result == WEB_SECURITY_LOGIN_LOCKED) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "login locked after repeated failures");
        secure_zero(pair_code, sizeof(pair_code));
        httpd_resp_set_status(req, "429 Too Many Requests");
        return httpd_resp_send(req, "login temporarily locked", HTTPD_RESP_USE_STRLEN);
    }
    if (result != WEB_SECURITY_LOGIN_OK) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "login failed");
        secure_zero(pair_code, sizeof(pair_code));
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid credentials");
    }
    if (pairing_pending && !local_pairing_verify_and_consume(&s_pairing, pair_code)) {
        web_security_logout(&s_security, s_security.session_token);
        secure_zero(pair_code, sizeof(pair_code));
        if (local_pairing_locked(&s_pairing)) {
            event_log_append(EVENT_LOG_SECURITY, now_ms(), "login rejected: pairing locked");
            notify_pairing_event(WEB_SERVER_PAIRING_LOCKED);
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid credentials");
        }
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "login rejected: invalid pair code");
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid credentials");
    }
    secure_zero(pair_code, sizeof(pair_code));
    if (pairing_pending) {
        notify_pairing_event(WEB_SERVER_PAIRING_CONSUMED);
    }

    event_log_append(EVENT_LOG_SECURITY, now_ms(), "login ok");
    runtime_status_set_locked(false);
    char cookie[96];
    snprintf(cookie, sizeof(cookie), "kvm_session=%s; HttpOnly; SameSite=Strict; Path=/", s_security.session_token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return redirect_to(req, "/terminal");
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");
    ESP_RETURN_ON_ERROR(validate_same_origin_header(req, "logout rejected: origin"), TAG, "origin rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    if (!request_authenticated(req, session_token)) {
        return redirect_to(req, "/login");
    }
    if (!csrf_header_valid(req, session_token)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid csrf");
    }
    web_security_logout(&s_security, session_token);
    runtime_status_set_writer_active(false);
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "logout");
    httpd_resp_set_hdr(req, "Set-Cookie", "kvm_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    return redirect_to(req, "/login");
}

static esp_err_t terminal_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    const char *csrf = web_security_csrf_for_session(&s_security, session_token, now_ms());
    if (csrf == NULL) {
        return redirect_to(req, "/login");
    }

    const usb_console_status_t usb = usb_console_get_status();
    const demo_serial_runtime_status_t demo = demo_serial_runtime_get_status();

    char terminal_page[9600];
    const int written = snprintf(
        terminal_page,
        sizeof(terminal_page),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Terminal</title><style>"
        ":root{--bg:#050b09;--panel:#0b1814;--line:#1f5b4b;--hot:#7dffe1;--warn:#ffcf7a;--bad:#ff875c;--text:#e9fff8;--muted:#8bb5aa}"
        "html,body{margin:0;height:100%%;background:radial-gradient(circle at 20%% -10%%,#143a30 0,#050b09 45%%);color:var(--text);font:15px ui-monospace,SFMono-Regular,Menlo,monospace}"
        "body{display:grid;grid-template-rows:auto 1fr auto;overflow:hidden}"
        "#bar{position:sticky;top:0;z-index:2;padding:10px 12px;background:rgba(5,11,9,.96);border-bottom:1px solid var(--line);box-shadow:0 10px 28px #0008}"
        "#top{display:flex;gap:8px;align-items:center;justify-content:space-between;flex-wrap:wrap}"
        "#brand{font-weight:800;letter-spacing:.04em}.pill{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--line);border-radius:999px;padding:7px 10px;background:#07110e;color:var(--muted)}"
        "#mode{font-weight:800;color:var(--hot)}#mode.write{color:#050b09;background:var(--hot)}#mode.busy,#mode.usb{color:#160b00;background:var(--warn)}#mode.locked{color:#1b0500;background:var(--bad)}"
        "#actions,#keys{display:flex;gap:8px;flex-wrap:wrap;margin-top:9px}"
        "button{min-height:42px;background:#102a23;color:var(--text);border:1px solid var(--line);border-radius:12px;padding:9px 12px;font:inherit;font-weight:700}"
        "button.primary{background:#123d32;border-color:var(--hot)}button.danger{border-color:var(--bad);color:#ffd2c0}button:disabled{opacity:.45}"
        "#term{white-space:pre-wrap;overflow:auto;padding:12px;box-sizing:border-box;background:#020604}"
        "#input{width:100%%;height:48px;box-sizing:border-box;border:0;border-top:1px solid var(--line);background:#07110e;color:#fff;padding:12px;font:16px ui-monospace,SFMono-Regular,Menlo,monospace}"
        "a{color:var(--hot)}@media(max-width:640px){html,body{font-size:14px}#bar{padding:8px}button{flex:1 1 27%%;min-height:46px;padding:10px 8px}#term{padding:10px}#input{height:52px}}"
        "</style></head><body><div id=\"bar\"><div id=\"top\"><span id=\"brand\">ESP32-KVM</span><span id=\"mode\" class=\"pill\">%s</span>"
        "<span id=\"state\" class=\"pill\">Connecting</span><a href=\"/runbook\">Runbook</a></div>"
        "<div id=\"actions\"><button class=\"primary\" id=\"write\">Request write</button><button id=\"release\">Release</button>"
        "<button id=\"demoStart\">Start demo</button><button id=\"demoStop\">Stop demo</button><button class=\"danger\" id=\"logout\">Logout</button></div>"
        "<div id=\"keys\"><button data-k=\"\\u0003\">%s</button><button data-k=\"\\u0004\">%s</button>"
        "<button data-k=\"\\r\">%s</button><button data-k=\"\\u001b\">%s</button><button data-k=\"\\t\">%s</button>"
        "<button data-k=\"\\u001b[A\">%s</button><button data-k=\"\\u001b[B\">%s</button>"
        "<button data-k=\"\\u001b[D\">%s</button><button data-k=\"\\u001b[C\">%s</button></div>"
        "</div><div id=\"term\"></div><input id=\"input\" autocomplete=\"off\" autocapitalize=\"none\" spellcheck=\"false\" placeholder=\"Read-only until write control is active\" autofocus>"
        "<script>"
        "const CSRF='%s';let canWrite=false,usbConnected=%s,demoActive=%s,writerState='read-only',locked=false,connected=false;"
        "const CHUNK=%u,PASTE_CONFIRM=%u,PASTE_MAX=%u;"
        "const term=document.getElementById('term'),input=document.getElementById('input'),state=document.getElementById('state');"
        "const mode=document.getElementById('mode'),writeBtn=document.getElementById('write'),releaseBtn=document.getElementById('release'),demoStart=document.getElementById('demoStart'),demoStop=document.getElementById('demoStop');"
        "function modeLabel(){if(locked)return'%s';if(demoActive)return'DEMO';if(!usbConnected)return'%s';if(writerState==='write-active')return'%s';if(writerState==='writer-busy')return'%s';return'%s'}"
        "function render(){const label=modeLabel();mode.textContent=label;mode.className='pill '+(label==='%s'?'write':label==='%s'?'busy':label==='%s'?'usb':label==='%s'?'locked':'');"
        "canWrite=connected&&usbConnected&&writerState==='write-active'&&!locked;input.disabled=!canWrite;writeBtn.disabled=locked||!connected||writerState==='write-active';releaseBtn.disabled=!canWrite;"
        "demoStart.disabled=locked||usbConnected||demoActive||writerState!=='read-only';demoStop.disabled=locked||!demoActive;"
        "input.placeholder=canWrite?'Type command, Enter sends CR':'Read-only until write control is active'}"
        "let ws,backoff=500;function add(t){term.textContent+=t;term.scrollTop=term.scrollHeight;if(term.textContent.length>65536)term.textContent=term.textContent.slice(-49152)}"
        "function connect(){state.textContent='Connecting';connected=false;render();ws=new WebSocket(`ws://${location.host}/ws`);ws.binaryType='arraybuffer';"
        "ws.onopen=()=>{state.textContent='Connected';connected=true;backoff=500;render();add('\\r\\n[web connected]\\r\\n')};"
        "ws.onmessage=e=>{if(e.data instanceof ArrayBuffer)add(new TextDecoder().decode(e.data));else add(e.data)};"
        "ws.onclose=()=>{connected=false;state.textContent='Reconnecting';render();setTimeout(connect,backoff);backoff=Math.min(backoff*2,5000)};"
        "ws.onerror=()=>ws.close()}"
        "async function refreshStatus(){try{const r=await fetch('/terminal-status.json',{cache:'no-store'});if(r.status===401||r.redirected){locked=true;render();return}if(!r.ok)return;const s=await r.json();usbConnected=!!s.usb_connected;demoActive=!!s.demo_active;writerState=s.writer_state||'read-only';locked=writerState==='locked';render()}catch(e){}}"
        "async function post(u){const r=await fetch(u,{method:'POST',headers:{'X-CSRF-Token':CSRF}});const t=await r.text();await refreshStatus();if(!r.ok&&u.includes('/api/demo/'))alert(t||('Request failed: '+r.status));return r.ok}"
        "function send(s){if(!(canWrite&&ws&&ws.readyState===1))return;for(let i=0;i<s.length;i+=CHUNK)ws.send(s.slice(i,i+CHUNK))}"
        "function safePaste(t){if(!canWrite)return;if(t.length>PASTE_MAX){alert('Paste too large; max '+PASTE_MAX+' bytes');return}"
        "if((t.length>PASTE_CONFIRM||t.includes('\\n'))&&!confirm('Send '+t.length+' bytes to the physical console?'))return;send(t)}"
        "input.addEventListener('keydown',e=>{if(e.key==='Enter'){send(input.value+'\\r');input.value='';e.preventDefault()}});"
        "input.addEventListener('paste',e=>{const t=(e.clipboardData||window.clipboardData).getData('text');if(t.length>PASTE_CONFIRM||t.includes('\\n')){e.preventDefault();safePaste(t)}});"
        "document.querySelectorAll('button[data-k]').forEach(b=>b.onclick=()=>send(b.dataset.k));"
        "writeBtn.onclick=async()=>{if(confirm('Writing here is equivalent to privileged physical console access. Continue?')){const ok=await post('/api/write/acquire');if(!ok&&writerState==='read-only')writerState='writer-busy';render()}};"
        "releaseBtn.onclick=async()=>{await post('/api/write/release');writerState='read-only';render()};"
        "demoStart.onclick=async()=>{if(confirm('Start local demo output? This never writes to USB.'))await post('/api/demo/start')};"
        "demoStop.onclick=async()=>{await post('/api/demo/stop')};"
        "document.getElementById('logout').onclick=async()=>{await post('/logout');location='/login'};"
        "render();refreshStatus();setInterval(refreshStatus,2000);connect();"
        "</script></body></html>",
        usb.connected ? WEB_TERMINAL_STATUS_READ_ONLY : WEB_TERMINAL_STATUS_USB_DISCONNECTED,
        WEB_TERMINAL_KEY_CTRL_C,
        WEB_TERMINAL_KEY_CTRL_D,
        WEB_TERMINAL_KEY_ENTER,
        WEB_TERMINAL_KEY_ESC,
        WEB_TERMINAL_KEY_TAB,
        WEB_TERMINAL_KEY_UP,
        WEB_TERMINAL_KEY_DOWN,
        WEB_TERMINAL_KEY_LEFT,
        WEB_TERMINAL_KEY_RIGHT,
        csrf,
        usb.connected ? "true" : "false",
        demo.active ? "true" : "false",
        (unsigned)WEB_INPUT_POLICY_FRAME_MAX,
        (unsigned)WEB_PASTE_CONFIRM_BYTES,
        (unsigned)WEB_PASTE_MAX_BYTES,
        WEB_TERMINAL_STATUS_LOCKED,
        WEB_TERMINAL_STATUS_USB_DISCONNECTED,
        WEB_TERMINAL_STATUS_WRITE_ACTIVE,
        WEB_TERMINAL_STATUS_WRITER_BUSY,
        WEB_TERMINAL_STATUS_READ_ONLY,
        WEB_TERMINAL_STATUS_WRITE_ACTIVE,
        WEB_TERMINAL_STATUS_WRITER_BUSY,
        WEB_TERMINAL_STATUS_USB_DISCONNECTED,
        WEB_TERMINAL_STATUS_LOCKED);
    if (written < 0 || written >= (int)sizeof(terminal_page)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "terminal overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, terminal_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t terminal_status_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    if (!request_authenticated(req, session_token)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "auth required");
    }

    const usb_console_status_t usb = usb_console_get_status();
    const demo_serial_runtime_status_t demo = demo_serial_runtime_get_status();
    const web_security_writer_state_t writer = web_security_writer_state(&s_security, session_token, now_ms());
    runtime_status_set_writer_active(writer == WEB_SECURITY_WRITER_ACTIVE);

    char body[192];
    const int written = snprintf(
        body,
        sizeof(body),
        "{\"usb_connected\":%s,\"demo_active\":%s,\"demo_bytes\":%llu,\"writer_state\":\"%s\"}",
        usb.connected ? "true" : "false",
        demo.active ? "true" : "false",
        (unsigned long long)demo.bytes_emitted,
        writer_state_name(writer));
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "terminal status overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t demo_start_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "demo start rejected: origin", "demo start rejected: csrf", session_token),
        TAG,
        "demo start auth failed");

    xSemaphoreTake(s_demo_writer_lock, portMAX_DELAY);
    const usb_console_status_t usb = usb_console_get_status();
    const web_security_writer_state_t writer = web_security_writer_state(&s_security, session_token, now_ms());
    if (!web_demo_policy_can_start(usb.connected, writer)) {
        xSemaphoreGive(s_demo_writer_lock);
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "demo serial start rejected: unsafe state");
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "demo unavailable while USB or writer is active", HTTPD_RESP_USE_STRLEN);
    }
    const esp_err_t err = demo_serial_runtime_enable(false);
    xSemaphoreGive(s_demo_writer_lock);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "demo unavailable while USB or writer is active", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t demo_stop_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "demo stop rejected: origin", "demo stop rejected: csrf", session_token),
        TAG,
        "demo stop auth failed");

    demo_serial_runtime_disable();
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t macro_run_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "macro run rejected: origin", "macro run rejected: csrf", session_token),
        TAG,
        "macro run auth failed");

    char body[HTTP_ROUTE_MACRO_RUN_BODY_MAX + 1];
    ESP_RETURN_ON_ERROR(read_small_body(req, body, sizeof(body)), TAG, "macro body failed");

    char macro_id[32];
    char confirm[8];
    const bool parsed = form_value(body, "id", macro_id, sizeof(macro_id)) &&
                        form_value(body, "confirm", confirm, sizeof(confirm));
    if (!parsed) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing macro confirmation");
    }

    const web_macro_descriptor_t *macro = web_macro_policy_find(macro_id);
    const usb_console_status_t usb = usb_console_get_status();
    const demo_serial_runtime_status_t demo = demo_serial_runtime_get_status();
    const bool writer_active = web_security_can_write(&s_security, session_token, now_ms());
    const bool user_confirmed = strcmp(confirm, "yes") == 0;
    if (!web_macro_policy_can_run(macro_id, s_macros_enabled, writer_active, usb.connected, demo.active, user_confirmed)) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "macro rejected");
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "macros disabled or unsafe state", HTTPD_RESP_USE_STRLEN);
    }

    const size_t command_len = strlen(macro->command);
    const size_t written = terminal_bridge_submit_input(TERMINAL_BRIDGE_SOURCE_WEB, (const uint8_t *)macro->command, command_len);
    if (written != command_len) {
        event_log_append(EVENT_LOG_WARN, now_ms(), "macro partially queued");
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "macro queue full", HTTPD_RESP_USE_STRLEN);
    }

    event_log_append(EVENT_LOG_SECURITY, now_ms(), "macro queued");
    return httpd_resp_sendstr(req, "queued");
}

static esp_err_t write_acquire_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "write acquire rejected: origin", "write acquire rejected: csrf", session_token),
        TAG,
        "write acquire auth failed");
    xSemaphoreTake(s_demo_writer_lock, portMAX_DELAY);
    if (!web_demo_policy_can_acquire_writer(demo_serial_runtime_get_status().active)) {
        xSemaphoreGive(s_demo_writer_lock);
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "write acquire rejected: demo active");
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "demo active", HTTPD_RESP_USE_STRLEN);
    }
    if (!web_security_acquire_writer(&s_security, session_token, now_ms())) {
        xSemaphoreGive(s_demo_writer_lock);
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "write acquire rejected: busy");
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "writer busy", HTTPD_RESP_USE_STRLEN);
    }
    xSemaphoreGive(s_demo_writer_lock);
    runtime_status_set_writer_active(true);
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "write control acquired");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t write_release_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "write release rejected: origin", "write release rejected: csrf", session_token),
        TAG,
        "write release auth failed");
    web_security_release_writer(&s_security, session_token);
    runtime_status_set_writer_active(false);
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "write control released");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t credentials_page_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    const char *csrf = web_security_csrf_for_session(&s_security, session_token, now_ms());
    if (csrf == NULL) {
        return redirect_to(req, "/login");
    }

    char body[2800];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Credentials</title><style>"
        "*{box-sizing:border-box}body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px;line-height:1.45}"
        "main{border:1px solid #174436;border-radius:18px;padding:18px;background:#030807;max-width:760px;width:100%%}"
        "button{font:inherit;border:1px solid #ff875c;border-radius:12px;background:#24110c;color:#ffd2c0;padding:12px 14px;margin:8px 0}"
        "a{color:#7dffe1}.warn{color:#ffcf7a}code{color:#bffff0}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style></head><body><main>"
        "<h1>Credential rotation</h1><p><a href=\"/\">Status</a> | <a href=\"/terminal\">Terminal</a> | <a href=\"/runbook\">Runbook</a></p>"
        "<p class=\"warn\">Rotation immediately invalidates web sessions and changes the next WiFi AP password. Secrets are never returned over HTTP.</p>"
        "<ol><li>Stay physically near the device.</li><li>Click rotate.</li><li>Read the new WiFi and web passwords on the AMOLED after pressing BOOT if needed.</li><li>Reboot to apply the new WiFi AP password.</li></ol>"
        "<button id=\"rotate\">Rotate credentials</button><button id=\"reboot\" %s>Reboot to apply WiFi</button><pre id=\"out\"></pre><script>"
        "const CSRF='%s',out=document.getElementById('out'),reboot=document.getElementById('reboot');let rebootPending=%s;"
        "function say(s){out.textContent+=s+'\\n'}"
        "document.getElementById('rotate').onclick=async()=>{if(!confirm('Rotate WiFi and web passwords now? Existing web sessions will be invalidated.'))return;"
        "const r=await fetch('/api/credentials/rotate',{method:'POST',headers:{'X-CSRF-Token':CSRF}});const t=await r.text();say(r.status+' '+t);if(r.ok){say('Session invalidated. Press BOOT, read AMOLED passwords, log in with the new web password, then return here to reboot.');setTimeout(()=>location='/login',2500)}};"
        "reboot.onclick=async()=>{if(!rebootPending){say('no pending credential reboot');return}if(confirm('Reboot now to apply the new WiFi password?')){const r=await fetch('/api/reboot',{method:'POST',headers:{'X-CSRF-Token':CSRF}});say(r.status+' '+await r.text())}};"
        "</script></main></body></html>",
        s_credential_reboot_pending ? "" : "disabled",
        csrf,
        s_credential_reboot_pending ? "true" : "false");
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "credentials page overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t credentials_rotate_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "credential rotation rejected: origin", "credential rotation rejected: csrf", session_token),
        TAG,
        "credential rotation auth failed");

    if (s_rotate_credentials == NULL) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "credential rotation unavailable");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rotation unavailable");
    }

    char wifi_password[WEB_SECURITY_PASSWORD_MAX_LEN];
    char web_password[WEB_SECURITY_PASSWORD_MAX_LEN];
    uint8_t web_salt[WEB_PASSWORD_SALT_LEN];
    uint8_t web_hash[WEB_PASSWORD_HASH_LEN];
    if (credentials_generate_human_password(wifi_password, sizeof(wifi_password), security_random, NULL) != CREDENTIALS_OK ||
        credentials_generate_human_password(web_password, sizeof(web_password), security_random, NULL) != CREDENTIALS_OK) {
        secure_zero(wifi_password, sizeof(wifi_password));
        secure_zero(web_password, sizeof(web_password));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "password generation failed");
    }
    if (!web_security_prepare_password_hash(web_password, security_random, NULL, web_salt, web_hash)) {
        secure_zero(wifi_password, sizeof(wifi_password));
        secure_zero(web_password, sizeof(web_password));
        secure_zero(web_salt, sizeof(web_salt));
        secure_zero(web_hash, sizeof(web_hash));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "password hash failed");
    }

    const web_server_credential_rotation_t rotation = {
        .wifi_password = wifi_password,
        .web_password = web_password,
        .web_password_salt = web_salt,
        .web_password_hash = web_hash,
        .reveal_on_local_display = true,
        .reboot_required = true,
    };
    esp_err_t err = s_rotate_credentials(&rotation, s_rotate_credentials_ctx);
    if (err == ESP_OK) {
        web_security_apply_password_hash(&s_security, web_salt, web_hash);
    }

    secure_zero(wifi_password, sizeof(wifi_password));
    secure_zero(web_password, sizeof(web_password));
    secure_zero(web_salt, sizeof(web_salt));
    secure_zero(web_hash, sizeof(web_hash));
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "credential rotation failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    runtime_status_set_writer_active(false);
    runtime_status_set_locked(true);
    s_credential_reboot_pending = true;
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "credentials rotated");
    send_no_store_headers(req);
    return httpd_resp_sendstr(req, "credentials rotated; read new passwords on local display and reboot to apply WiFi");
}

static esp_err_t config_page_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    const char *csrf = web_security_csrf_for_session(&s_security, session_token, now_ms());
    if (csrf == NULL) {
        return redirect_to(req, "/login");
    }

    char body[3000];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Config</title><style>"
        "*{box-sizing:border-box}body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px;line-height:1.45}"
        "main{border:1px solid #174436;border-radius:18px;padding:18px;background:#030807;max-width:760px;width:100%%}"
        "button,textarea{font:inherit}button{border:1px solid #2ee6b8;border-radius:12px;background:#123d32;color:#bffff0;padding:11px 14px;margin:8px 8px 8px 0}"
        "textarea{width:100%%;min-height:160px;background:#000;color:#e9fff8;border:1px solid #245c4c;border-radius:12px;padding:12px}"
        "a{color:#7dffe1}.warn{color:#ffcf7a}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style></head><body><main>"
        "<h1>Configuration</h1><p><a href=\"/\">Status</a> | <a href=\"/terminal\">Terminal</a> | <a href=\"/config.json\">Export JSON</a></p>"
        "<p class=\"warn\">Export excludes WiFi password, web password hash, salts and serial data. Import validates schema and checksum, then requires reboot to apply AP changes.</p>"
        "<textarea id=\"cfg\" placeholder=\"Paste esp32-kvm config JSON here\"></textarea>"
        "<p><button id=\"load\">Load current export</button><button id=\"import\">Import config</button></p><pre id=\"out\"></pre><script>"
        "const CSRF='%s',out=document.getElementById('out'),cfg=document.getElementById('cfg');"
        "function say(s){out.textContent+=s+'\\n'}"
        "document.getElementById('load').onclick=async()=>{const r=await fetch('/config.json'),t=await r.text();"
        "if(!r.ok||!(r.headers.get('content-type')||'').includes('application/json')){say('export failed: '+r.status+' '+t);return}"
        "cfg.value=t;say(r.status+' export loaded')};"
        "document.getElementById('import').onclick=async()=>{if(!confirm('Import non-secret config and require reboot?'))return;"
        "const r=await fetch('/api/config/import',{method:'POST',headers:{'X-CSRF-Token':CSRF,'Content-Type':'application/json'},body:cfg.value});say(r.status+' '+await r.text())};"
        "</script></main></body></html>",
        csrf);
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config page overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_json_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    if (s_export_config == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config export unavailable");
    }

    char body[HTTP_ROUTE_CONFIG_IMPORT_BODY_MAX];
    const esp_err_t err = s_export_config(body, sizeof(body), s_config_ctx);
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "config export failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_import_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "config import rejected: origin", "config import rejected: csrf", session_token),
        TAG,
        "config import auth failed");
    if (s_import_config == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config import unavailable");
    }

    char body[HTTP_ROUTE_CONFIG_IMPORT_BODY_MAX + 1U];
    esp_err_t err = read_small_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return err;
    }

    err = s_import_config(body, s_config_ctx);
    secure_zero(body, sizeof(body));
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "config import rejected");
        if (err != ESP_ERR_INVALID_ARG) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "import failed: config could not be saved safely; no changes saved");
        }
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "import rejected: invalid JSON, schema, checksum, or unsupported value; no changes saved");
    }

    s_credential_reboot_pending = true;
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "config imported; reboot pending");
    send_no_store_headers(req);
    return httpd_resp_sendstr(req, "config imported; reboot required to apply AP settings");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");
    const char *csrf = web_security_csrf_for_session(&s_security, session_token, now_ms());
    if (csrf == NULL) {
        return redirect_to(req, "/login");
    }

    const ota_update_status_t ota = ota_update_get_status();
    char body[3600];
    const int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM OTA</title><style>"
        "*{box-sizing:border-box}body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px;line-height:1.45}"
        "main{border:1px solid #174436;border-radius:18px;padding:18px;background:#030807;max-width:760px;width:100%%}"
        "button,input{font:inherit}button{border:1px solid #2ee6b8;border-radius:12px;background:#123d32;color:#bffff0;padding:11px 14px;margin:8px 8px 8px 0}"
        "button:disabled{opacity:.45}button.danger{border-color:#ff875c;color:#ffd2c0;background:#24110c}input[type=file]{max-width:100%%}"
        "code{color:#bffff0}a{color:#7dffe1}.warn{color:#ffcf7a}@media(max-width:480px){button{display:block;width:100%%}}</style></head><body><main>"
        "<h1>Firmware update</h1><p><a href=\"/\">Status</a> | <a href=\"/terminal\">Terminal</a> | <a href=\"/about\">About</a></p>"
        "<p class=\"warn\">Manual local OTA only. Upload a complete ESP-IDF app image built for this board. In production, Secure Boot rejects unsigned or wrongly signed images.</p>"
        "<dl><dt>State</dt><dd id=\"otaState\">%s</dd><dt>Target slot</dt><dd>%s</dd><dt>Bytes</dt><dd>%u / %u</dd><dt>Last error</dt><dd>%s</dd></dl>"
        "<input id=\"fw\" type=\"file\" accept=\".bin,application/octet-stream\">"
        "<p><button id=\"upload\">Upload firmware</button><button class=\"danger\" id=\"reboot\" %s>Reboot to pending image</button></p>"
        "<pre id=\"out\"></pre><script>"
        "const CSRF='%s',out=document.getElementById('out'),otaState=document.getElementById('otaState'),reboot=document.getElementById('reboot');let pending=%s;"
        "function say(s){out.textContent+=s+'\\n'}"
        "async function post(u,b){const r=await fetch(u,{method:'POST',headers:{'X-CSRF-Token':CSRF,'Content-Type':'application/octet-stream'},body:b});const t=await r.text();say(r.status+' '+t);return r.ok}"
        "document.getElementById('upload').onclick=async()=>{const f=document.getElementById('fw').files[0];if(!f){say('choose firmware first');return}"
        "if(f.size===0||f.size>%u){say('invalid size; max %u bytes');return}"
        "if(!confirm('Install '+f.name+' ('+f.size+' bytes) on the inactive OTA slot?'))return;"
        "const ok=await post('/api/ota',f);if(ok){pending=true;otaState.textContent='pending reboot';reboot.disabled=false;say('upload accepted; reboot explicitly when ready')}};"
        "reboot.onclick=async()=>{if(!pending){say('no pending image');return}if(confirm('Reboot ESP32-KVM now? Serial bridge will disconnect briefly.'))await post('/api/reboot',null)};"
        "</script></main></body></html>",
        ota.in_progress ? "uploading" : (ota.pending_reboot ? "pending reboot" : "idle"),
        ota.target_label[0] != '\0' ? ota.target_label : "next OTA slot",
        (unsigned)ota.written_bytes,
        (unsigned)ota.expected_bytes,
        esp_err_to_name(ota.last_error),
        ota_update_policy_reboot_allowed(ota.pending_reboot, ota.in_progress) ? "" : "disabled",
        csrf,
        ota_update_policy_reboot_allowed(ota.pending_reboot, ota.in_progress) ? "true" : "false",
        (unsigned)OTA_UPDATE_MAX_IMAGE_BYTES,
        (unsigned)OTA_UPDATE_MAX_IMAGE_BYTES);
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota page overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "ota upload rejected: origin", "ota upload rejected: csrf", session_token),
        TAG,
        "ota upload auth failed");

    const ota_update_status_t status = ota_update_get_status();
    const ota_update_policy_result_t policy = ota_update_policy_evaluate(req->content_len, status.in_progress);
    if (policy != OTA_UPDATE_POLICY_ACCEPT) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), ota_update_policy_result_name(policy));
        if (policy == OTA_UPDATE_POLICY_REJECT_BUSY) {
            httpd_resp_set_status(req, "409 Conflict");
            return httpd_resp_send(req, "ota busy", HTTPD_RESP_USE_STRLEN);
        }
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, ota_update_policy_result_name(policy));
    }

    ota_update_session_t session = {0};
    esp_err_t err = ota_update_begin(req->content_len, &session);
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "ota begin failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    uint8_t buf[WEB_OTA_RECV_CHUNK];
    size_t received = 0;
    unsigned consecutive_timeouts = 0;
    const uint64_t upload_start_ms = now_ms();
    while (received < req->content_len) {
        if (ota_update_policy_deadline_exceeded(upload_start_ms, now_ms())) {
            ota_update_abort(&session);
            event_log_append(EVENT_LOG_ERROR, now_ms(), "ota upload deadline exceeded");
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_send(req, "upload deadline exceeded", HTTPD_RESP_USE_STRLEN);
        }
        const size_t remaining = req->content_len - received;
        const size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        const int got = httpd_req_recv(req, (char *)buf, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            consecutive_timeouts++;
            if (ota_update_policy_timeout_exhausted(consecutive_timeouts)) {
                ota_update_abort(&session);
                event_log_append(EVENT_LOG_ERROR, now_ms(), "ota upload timed out");
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "upload timed out", HTTPD_RESP_USE_STRLEN);
            }
            continue;
        }
        if (got <= 0) {
            ota_update_abort(&session);
            event_log_append(EVENT_LOG_ERROR, now_ms(), "ota upload read failed");
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload read failed");
        }
        consecutive_timeouts = 0;
        err = ota_update_write(&session, buf, (size_t)got);
        if (err != ESP_OK) {
            ota_update_abort(&session);
            event_log_append(EVENT_LOG_ERROR, now_ms(), "ota write failed");
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        }
        received += (size_t)got;
    }
    memset(buf, 0, sizeof(buf));

    err = ota_update_finish(&session);
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "ota validation failed");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }

    event_log_append(EVENT_LOG_SECURITY, now_ms(), "ota image accepted; reboot pending");
    send_no_store_headers(req);
    return httpd_resp_sendstr(req, "ota accepted; reboot required");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(
        require_mutating_auth_csrf_origin(req, "reboot rejected: origin", "reboot rejected: csrf", session_token),
        TAG,
        "reboot auth failed");

    const ota_update_status_t ota = ota_update_get_status();
    if (!ota_update_policy_reboot_allowed(ota.pending_reboot, ota.in_progress) && !s_credential_reboot_pending) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "reboot rejected: no pending action");
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "no pending reboot action", HTTPD_RESP_USE_STRLEN);
    }
    if (xTaskCreate(reboot_task, "web_reboot", 2048, NULL, 5, NULL) != pdPASS) {
        event_log_append(EVENT_LOG_ERROR, now_ms(), "reboot task creation failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reboot task failed");
    }
    event_log_append(EVENT_LOG_SECURITY, now_ms(), "manual reboot requested");
    send_no_store_headers(req);
    return httpd_resp_sendstr(req, "rebooting");
}

static esp_err_t diagnostics_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    ESP_RETURN_ON_ERROR(require_auth_or_redirect(req, session_token), TAG, "auth failed");

    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    const event_log_status_t log_status = event_log_get_status();
    event_log_entry_t events[WEB_DIAG_EVENT_LIMIT];
    const size_t event_count = event_log_snapshot(events, WEB_DIAG_EVENT_LIMIT);

    char body[4096];
    int written = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Diagnostics</title><style>"
        "body{background:#050b09;color:#e9fff8;font:15px sans-serif;margin:20px}"
        "code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}"
        ".card{border:1px solid #174436;border-radius:16px;padding:14px;margin:12px 0;background:#030807}"
        "dt{color:#7dffe1}dd{margin:0 0 8px 0}</style></head><body>"
        "<h1>Diagnostics</h1><p><a href=\"/terminal\">Terminal</a> | <a href=\"/\">Status</a> | <a href=\"/runbook\">Runbook</a> | <a href=\"/diagnostics.json\">JSON</a></p>"
        "<section class=\"card\"><h2>Runtime</h2><dl>"
        "<dt>Uptime ms</dt><dd>%llu</dd>"
        "<dt>Reset reason</dt><dd>%d</dd>"
        "<dt>Heap free</dt><dd>%u</dd>"
        "<dt>Heap minimum</dt><dd>%u</dd>"
        "<dt>Event log retained/written/dropped</dt><dd>%u / %u / %u</dd>"
        "</dl></section>"
        "<section class=\"card\"><h2>Network and USB</h2><dl>"
        "<dt>AP</dt><dd>%s %s clients=%u</dd>"
        "<dt>USB</dt><dd>%s rx=%llu tx=%llu</dd>"
        "<dt>Bridge</dt><dd>usb_rx=%llu usb_tx=%llu drop_rx=%llu drop_tx=%llu subscribers=%u scrollback=%u/%u dropped_old=%llu</dd>"
        "</dl></section>"
        "<section class=\"card\"><h2>Recent events</h2><pre>",
        (unsigned long long)now_ms(),
        (int)esp_reset_reason(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)log_status.retained,
        (unsigned)log_status.written,
        (unsigned)log_status.dropped_oldest,
        wifi.started ? "active" : "inactive",
        wifi.ip_addr,
        wifi.connected_clients,
        usb.connected ? "connected" : "disconnected",
        (unsigned long long)usb.bytes_received,
        (unsigned long long)usb.bytes_sent,
        (unsigned long long)bridge.bytes_from_usb,
        (unsigned long long)bridge.bytes_to_usb,
        (unsigned long long)bridge.dropped_from_usb,
        (unsigned long long)bridge.dropped_to_usb,
        (unsigned)bridge.subscriber_count,
        (unsigned)bridge.scrollback_retained,
        (unsigned)bridge.scrollback_capacity,
        (unsigned long long)bridge.scrollback_dropped_oldest);
    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "diagnostics overflow");
    }

    for (size_t i = 0; i < event_count; ++i) {
        const int remaining = (int)sizeof(body) - written;
        if (remaining <= 1) {
            break;
        }
        char escaped_message[EVENT_LOG_MESSAGE_MAX * 6];
        if (diagnostics_export_write_html_escaped(escaped_message, sizeof(escaped_message), events[i].message) < 0) {
            strncpy(escaped_message, "[event message unavailable]", sizeof(escaped_message) - 1);
            escaped_message[sizeof(escaped_message) - 1] = '\0';
        }

        const int add = snprintf(
            body + written,
            (size_t)remaining,
            "#%u %llums [%s] %s\n",
            (unsigned)events[i].sequence,
            (unsigned long long)events[i].timestamp_ms,
            level_name(events[i].level),
            escaped_message);
        if (add < 0 || add >= remaining) {
            break;
        }
        written += add;
    }

    const char tail[] = "</pre></section></body></html>";
    if ((size_t)written + sizeof(tail) > sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "diagnostics overflow");
    }
    memcpy(body + written, tail, sizeof(tail));

    send_no_store_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t diagnostics_json_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
    ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    if (!request_authenticated(req, session_token)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "auth required");
    }

    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    event_log_entry_t events[WEB_DIAG_EVENT_LIMIT];
    const size_t event_count = event_log_snapshot(events, WEB_DIAG_EVENT_LIMIT);

    const diagnostics_export_snapshot_t snapshot = {
        .uptime_ms = now_ms(),
        .reset_reason = (int)esp_reset_reason(),
        .heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .heap_minimum = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        .event_log = event_log_get_status(),
        .ap_started = wifi.started,
        .ap_ip = wifi.ip_addr,
        .wifi_clients = wifi.connected_clients,
        .usb_connected = usb.connected,
        .usb_bytes_received = usb.bytes_received,
        .usb_bytes_sent = usb.bytes_sent,
        .bridge_bytes_from_usb = bridge.bytes_from_usb,
        .bridge_bytes_to_usb = bridge.bytes_to_usb,
        .bridge_dropped_from_usb = bridge.dropped_from_usb,
        .bridge_dropped_to_usb = bridge.dropped_to_usb,
        .bridge_subscribers = bridge.subscriber_count,
        .events = events,
        .event_count = event_count,
    };

    char body[4096];
    if (diagnostics_export_write_json(body, sizeof(body), &snapshot) < 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "diagnostics json overflow");
    }

    send_no_store_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_RETURN_ON_ERROR(enforce_http_rate_limit(req), TAG, "http rate limited");
        ESP_RETURN_ON_ERROR(validate_route_policy(req), TAG, "route rejected");
    }

    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    if (!request_authenticated(req, session_token)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "auth required");
    }

    if (req->method == HTTP_GET) {
        char origin[WEB_HEADER_VALUE_MAX];
        char host[WEB_HEADER_VALUE_MAX];
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
            httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
            !web_security_origin_allowed(origin, host)) {
            event_log_append(EVENT_LOG_SECURITY, now_ms(), "websocket rejected: origin");
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "invalid origin");
        }

        const int fd = httpd_req_to_sockfd(req);
        if (add_ws_fd(fd) != ESP_OK) {
            event_log_append(EVENT_LOG_WARN, now_ms(), "websocket rejected: too many clients");
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "too many websocket clients");
        }
        ESP_LOGI(TAG, "websocket client connected fd=%d", fd);
        event_log_append(EVENT_LOG_INFO, now_ms(), "websocket client connected");
        send_scrollback_to_ws_client(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = NULL,
        .len = 0,
    };
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, 0), TAG, "ws frame header failed");
    if (frame.len > WEB_INPUT_POLICY_FRAME_MAX) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "websocket input rejected: frame too large");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "websocket frame too large");
    }

    uint8_t payload[WEB_INPUT_POLICY_FRAME_MAX];
    frame.payload = payload;
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, frame.len), TAG, "ws frame payload failed");
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_ws_fd(httpd_req_to_sockfd(req));
        event_log_append(EVENT_LOG_INFO, now_ms(), "websocket client closed");
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_BINARY && frame.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }

    if (!web_security_can_write(&s_security, session_token, now_ms())) {
        ESP_LOGW(TAG, "dropping websocket input without write lock");
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "websocket input dropped: read-only");
        return ESP_OK;
    }
    if (demo_serial_runtime_get_status().active) {
        ESP_LOGW(TAG, "dropping websocket input while demo is active");
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "websocket input dropped: demo active");
        return ESP_OK;
    }

    const web_input_policy_result_t policy = web_input_policy_evaluate(&s_input_policy, frame.len, now_ms());
    if (policy != WEB_INPUT_POLICY_ACCEPT) {
        ESP_LOGW(TAG, "dropping websocket input: %s", web_input_policy_result_name(policy));
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "websocket input rejected by policy");
        return ESP_OK;
    }

    const size_t written = terminal_bridge_submit_input(TERMINAL_BRIDGE_SOURCE_WEB, payload, frame.len);
    if (written < frame.len) {
        ESP_LOGW(TAG, "bridge input queue full; accepted %u/%u bytes", (unsigned)written, (unsigned)frame.len);
        event_log_append(EVENT_LOG_WARN, now_ms(), "bridge input queue full");
    }
    return ESP_OK;
}

static esp_err_t http_error_handler(httpd_req_t *req, httpd_err_code_t error)
{
    if (enforce_http_rate_limit(req) != ESP_OK) {
        return ESP_OK;
    }

    http_route_policy_result_t result = HTTP_ROUTE_POLICY_REJECT_NOT_FOUND;
    if (error == HTTPD_405_METHOD_NOT_ALLOWED) {
        result = HTTP_ROUTE_POLICY_REJECT_METHOD;
    }
    return send_route_policy_error(req, result);
}

esp_err_t web_server_start(const web_server_config_t *server_config)
{
    ESP_RETURN_ON_FALSE(server_config != NULL, ESP_ERR_INVALID_ARG, TAG, "missing web config");
    bool auth_ok = false;
    if (server_config->web_password_hash_configured) {
        auth_ok = web_security_init_from_hash(&s_security, server_config->web_password_salt, server_config->web_password_hash);
    } else {
        auth_ok = web_security_init(&s_security, server_config->web_password, security_random, NULL);
    }
    ESP_RETURN_ON_FALSE(auth_ok, ESP_ERR_INVALID_ARG, TAG, "invalid web auth config");
    web_input_policy_init(&s_input_policy);
    web_terminal_ansi_init(&s_web_ansi_state);
    ap_exposure_policy_init(&s_ap_exposure_policy);
    http_rate_limit_init(&s_http_rate_limit);
    s_http_rate_limit_lock = xSemaphoreCreateMutexStatic(&s_http_rate_limit_lock_storage);
    ESP_RETURN_ON_FALSE(s_http_rate_limit_lock != NULL, ESP_ERR_NO_MEM, TAG, "http rate lock failed");
    s_ws_fds_lock = xSemaphoreCreateMutexStatic(&s_ws_fds_lock_storage);
    ESP_RETURN_ON_FALSE(s_ws_fds_lock != NULL, ESP_ERR_NO_MEM, TAG, "websocket fd lock failed");
    s_demo_writer_lock = xSemaphoreCreateMutexStatic(&s_demo_writer_lock_storage);
    ESP_RETURN_ON_FALSE(s_demo_writer_lock != NULL, ESP_ERR_NO_MEM, TAG, "demo writer lock failed");
    event_log_init();
    event_log_append(EVENT_LOG_INFO, now_ms(), "web server starting");
    s_rotate_credentials = server_config->rotate_credentials;
    s_rotate_credentials_ctx = server_config->rotate_credentials_ctx;
    s_pairing_event = server_config->pairing_event;
    s_pairing_event_ctx = server_config->pairing_event_ctx;
    s_export_config = server_config->export_config;
    s_import_config = server_config->import_config;
    s_config_ctx = server_config->config_ctx;
    s_macros_enabled = web_macro_policy_default_enabled();
    ESP_RETURN_ON_FALSE(local_pairing_init(&s_pairing, server_config->pairing_code), ESP_ERR_INVALID_ARG, TAG, "invalid pairing config");

    if (!wifi_ap_get_status().started) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_handle_t server = NULL;
    bool tls_server = false;
    if (server_config->tls_identity != NULL) {
        const https_fingerprint_policy_request_t https_policy = {
            .requested = true,
            .certificate_present = server_config->tls_identity->cert_pem_len > 0U &&
                                   server_config->tls_identity->key_pem_len > 0U,
            .fingerprint_displayed_locally = server_config->tls_fingerprint_displayed_locally,
            .operator_acknowledged_fingerprint = false,
        };
        const https_fingerprint_policy_result_t https_result = https_fingerprint_policy_can_start_listener(&https_policy);
        if (https_result == HTTPS_FINGERPRINT_POLICY_ALLOW) {
            httpd_ssl_config_t https_config = HTTPD_SSL_CONFIG_DEFAULT();
            https_config.httpd.lru_purge_enable = true;
            https_config.httpd.max_uri_handlers = 25;
            https_config.servercert = (const uint8_t *)server_config->tls_identity->cert_pem;
            https_config.servercert_len = server_config->tls_identity->cert_pem_len + 1U;
            https_config.prvtkey_pem = (const uint8_t *)server_config->tls_identity->key_pem;
            https_config.prvtkey_len = server_config->tls_identity->key_pem_len + 1U;
            const esp_err_t tls_err = httpd_ssl_start(&server, &https_config);
            if (tls_err == ESP_OK) {
                tls_server = true;
                event_log_append(EVENT_LOG_INFO, now_ms(), "https server started");
            } else {
                ESP_LOGE(TAG, "https start failed after displaying fingerprint: %s", esp_err_to_name(tls_err));
                event_log_append(EVENT_LOG_ERROR, now_ms(), "https start failed after fingerprint display");
                return tls_err;
            }
        } else {
            ESP_LOGW(TAG, "https disabled by policy: %s", https_fingerprint_policy_result_name(https_result));
            event_log_append(EVENT_LOG_WARN, now_ms(), "https disabled by policy");
        }
    }
    if (server == NULL) {
        httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
        http_config.lru_purge_enable = true;
        http_config.max_uri_handlers = 25;
        ESP_RETURN_ON_ERROR(httpd_start(&server, &http_config), TAG, "httpd_start failed");
    }
    s_server = server;
    s_server_tls = tls_server;
    runtime_status_set_started(true);
    runtime_status_set_tls_active(tls_server);
    runtime_status_set_locked(false);
    runtime_status_set_writer_active(false);
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        s_ws_fds[i] = -1;
    }
    s_tx_queue = xQueueCreate(WEB_TX_QUEUE_DEPTH, sizeof(web_tx_chunk_t));
    if (s_tx_queue == NULL) {
        cleanup_failed_start(server);
        return ESP_ERR_NO_MEM;
    }

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t terminal_uri = {
        .uri = "/terminal",
        .method = HTTP_GET,
        .handler = terminal_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t terminal_status_uri = {
        .uri = "/terminal-status.json",
        .method = HTTP_GET,
        .handler = terminal_status_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t about_uri = {
        .uri = "/about",
        .method = HTTP_GET,
        .handler = about_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t runbook_uri = {
        .uri = "/runbook",
        .method = HTTP_GET,
        .handler = runbook_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t ota_page_uri = {
        .uri = "/ota",
        .method = HTTP_GET,
        .handler = ota_page_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t credentials_page_uri = {
        .uri = "/credentials",
        .method = HTTP_GET,
        .handler = credentials_page_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t config_page_uri = {
        .uri = "/config",
        .method = HTTP_GET,
        .handler = config_page_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t config_json_uri = {
        .uri = "/config.json",
        .method = HTTP_GET,
        .handler = config_json_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t macros_page_uri = {
        .uri = "/macros",
        .method = HTTP_GET,
        .handler = macros_page_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t credentials_rotate_uri = {
        .uri = "/api/credentials/rotate",
        .method = HTTP_POST,
        .handler = credentials_rotate_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t config_import_uri = {
        .uri = "/api/config/import",
        .method = HTTP_POST,
        .handler = config_import_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t macro_run_uri = {
        .uri = "/api/macros/run",
        .method = HTTP_POST,
        .handler = macro_run_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t ota_upload_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t reboot_uri = {
        .uri = "/api/reboot",
        .method = HTTP_POST,
        .handler = reboot_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t diagnostics_uri = {
        .uri = "/diagnostics",
        .method = HTTP_GET,
        .handler = diagnostics_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t diagnostics_json_uri = {
        .uri = "/diagnostics.json",
        .method = HTTP_GET,
        .handler = diagnostics_json_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t login_get_uri = {
        .uri = "/login",
        .method = HTTP_GET,
        .handler = login_get_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t login_post_uri = {
        .uri = "/login",
        .method = HTTP_POST,
        .handler = login_post_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t logout_uri = {
        .uri = "/logout",
        .method = HTTP_POST,
        .handler = logout_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t write_acquire_uri = {
        .uri = "/api/write/acquire",
        .method = HTTP_POST,
        .handler = write_acquire_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t write_release_uri = {
        .uri = "/api/write/release",
        .method = HTTP_POST,
        .handler = write_release_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t demo_start_uri = {
        .uri = "/api/demo/start",
        .method = HTTP_POST,
        .handler = demo_start_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t demo_stop_uri = {
        .uri = "/api/demo/stop",
        .method = HTTP_POST,
        .handler = demo_stop_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &index_uri), fail, TAG, "index handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &login_get_uri), fail, TAG, "login get handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &login_post_uri), fail, TAG, "login post handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &logout_uri), fail, TAG, "logout handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &terminal_uri), fail, TAG, "terminal handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &terminal_status_uri), fail, TAG, "terminal status handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &about_uri), fail, TAG, "about handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &runbook_uri), fail, TAG, "runbook handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &credentials_page_uri), fail, TAG, "credentials page handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &config_page_uri), fail, TAG, "config page handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &config_json_uri), fail, TAG, "config json handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &macros_page_uri), fail, TAG, "macros page handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &ota_page_uri), fail, TAG, "ota page handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &diagnostics_uri), fail, TAG, "diagnostics handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &diagnostics_json_uri), fail, TAG, "diagnostics json handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &write_acquire_uri), fail, TAG, "write acquire handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &write_release_uri), fail, TAG, "write release handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &demo_start_uri), fail, TAG, "demo start handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &demo_stop_uri), fail, TAG, "demo stop handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &ota_upload_uri), fail, TAG, "ota upload handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &reboot_uri), fail, TAG, "reboot handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &credentials_rotate_uri), fail, TAG, "credentials rotate handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &config_import_uri), fail, TAG, "config import handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &macro_run_uri), fail, TAG, "macro run handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &ws_uri), fail, TAG, "websocket handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_error_handler), fail, TAG, "404 handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, http_error_handler), fail, TAG, "405 handler failed");
    BaseType_t task_ok = xTaskCreate(web_tx_task, "web_tx", 4096, NULL, 3, &s_web_tx_task);
    ESP_GOTO_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, fail, TAG, "web tx task failed");
    task_ok = xTaskCreate(ap_guard_task, "ap_guard", WEB_AP_GUARD_TASK_STACK, NULL, WEB_AP_GUARD_TASK_PRIORITY, &s_ap_guard_task);
    ESP_GOTO_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, fail, TAG, "ap guard task failed");
    ESP_GOTO_ON_ERROR(terminal_bridge_register_output_callback(bridge_output_cb, NULL), fail, TAG, "bridge callback failed");
    ESP_GOTO_ON_ERROR(demo_serial_runtime_start(), fail, TAG, "demo serial runtime failed");

    ESP_LOGI(TAG, "%s server started", tls_server ? "HTTPS" : "HTTP");
    return ESP_OK;

fail:
    cleanup_failed_start(server);
    return ret;
}

esp_err_t web_server_emergency_lock(void)
{
    if (s_server == NULL) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "emergency lock ignored before http start");
        return ESP_OK;
    }

    demo_serial_runtime_disable();
    const esp_err_t err = httpd_queue_work(s_server, emergency_lock_work, NULL);
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_SECURITY, now_ms(), "emergency lock queue failed; stopping AP");
        const esp_err_t stop_err = wifi_ap_stop();
        if (stop_err != ESP_OK) {
            ESP_LOGE(TAG, "emergency lock could not stop AP: %s; firmware kept alive for local recovery", esp_err_to_name(stop_err));
            event_log_append(EVENT_LOG_ERROR, now_ms(), "emergency lock AP stop failed; firmware kept alive");
        }
    }
    return err;
}

web_server_status_t web_server_get_status(void)
{
    web_server_status_t status;
    portENTER_CRITICAL(&s_runtime_status_lock);
    status = s_runtime_status;
    portEXIT_CRITICAL(&s_runtime_status_lock);
    return status;
}
