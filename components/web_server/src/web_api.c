/* JSON API handlers. The dispatcher has already applied each route's guard. */

#include <string.h>

#include "diagnostics_export.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "event_log.h"
#include "json_writer.h"
#include "ota_update.h"
#include "ota_update_policy.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_internal.h"
#include "wifi_ap.h"

static const char *TAG = "web_api";

#define API_JSON_MAX 640
#define API_CONFIG_MAX 513
#define API_DIAG_JSON_MAX 4096
#define API_DIAG_EVENTS 16
#define API_OTA_CHUNK 2048

static esp_err_t reply_writer(web_request_t *r, json_writer_t *w)
{
    if (!json_writer_finish(w)) {
        return web_reply_text(r, "500 Internal Server Error", "response too large");
    }
    return web_reply_json(r, w->buf);
}

static esp_err_t read_body(httpd_req_t *req, char *out, size_t out_size)
{
    if (req->content_len >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, out + received, req->content_len - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)got;
    }
    out[received] = '\0';
    return ESP_OK;
}

esp_err_t web_api_session(web_request_t *r)
{
    if (r->session_token[0] == '\0' && !web_session_start(r)) {
        return web_reply_text(r, "503 Service Unavailable", "session unavailable");
    }
    const char *csrf = web_security_csrf_for_session(&g_web.security, r->session_token, web_now_ms());
    char json[API_JSON_MAX];
    json_writer_t w;
    json_writer_init(&w, json, sizeof(json));
    json_writer_raw(&w, "{\"csrf\":");
    json_writer_string(&w, csrf);
    json_writer_raw(&w, ",\"usb_connected\":");
    json_writer_bool(&w, usb_console_get_status().connected);
    json_writer_raw(&w, ",\"tls\":");
    json_writer_bool(&w, g_web.site.tls);
    json_writer_raw(&w, "}");
    return reply_writer(r, &w);
}

esp_err_t web_api_logout(web_request_t *r)
{
    web_session_end(r);
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "web session ended");
    return web_reply_text(r, "200 OK", "signed out");
}

esp_err_t web_api_status(web_request_t *r)
{
    char json[API_JSON_MAX];
    json_writer_t w;
    json_writer_init(&w, json, sizeof(json));
    json_writer_raw(&w, "{\"usb_connected\":");
    json_writer_bool(&w, usb_console_get_status().connected);
    json_writer_raw(&w, ",\"locked\":");
    json_writer_bool(&w, g_web.locked);
    json_writer_raw(&w, "}");
    return reply_writer(r, &w);
}

esp_err_t web_api_about(web_request_t *r)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    char json[API_JSON_MAX];
    json_writer_t w;
    json_writer_init(&w, json, sizeof(json));
    json_writer_raw(&w, "{\"version\":");
    json_writer_string(&w, app->version);
    json_writer_raw(&w, ",\"project\":");
    json_writer_string(&w, app->project_name);
    json_writer_raw(&w, ",\"build_date\":");
    json_writer_string(&w, app->date);
    json_writer_raw(&w, ",\"build_time\":");
    json_writer_string(&w, app->time);
    json_writer_raw(&w, ",\"idf\":");
    json_writer_string(&w, app->idf_ver);
    json_writer_raw(&w, ",\"https\":");
    json_writer_bool(&w, g_web.site.tls);
    json_writer_raw(
        &w,
        ",\"scrollback\":{\"retained\":%u,\"capacity\":%u,\"dropped\":%llu}}",
        (unsigned)bridge.scrollback_retained,
        (unsigned)bridge.scrollback_capacity,
        (unsigned long long)bridge.scrollback_dropped_oldest);
    return reply_writer(r, &w);
}

esp_err_t web_api_diagnostics(web_request_t *r)
{
    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    event_log_entry_t events[API_DIAG_EVENTS];
    const size_t event_count = event_log_snapshot(events, API_DIAG_EVENTS);

    const diagnostics_export_snapshot_t snapshot = {
        .uptime_ms = web_now_ms(),
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

    static char json[API_DIAG_JSON_MAX];
    if (diagnostics_export_write_json(json, sizeof(json), &snapshot) < 0) {
        return web_reply_text(r, "500 Internal Server Error", "diagnostics too large");
    }
    return web_reply_json(r, json);
}

esp_err_t web_api_config_export(web_request_t *r)
{
    if (g_web.config.export_config == NULL) {
        return web_reply_text(r, "501 Not Implemented", "config export unavailable");
    }
    char json[API_CONFIG_MAX];
    if (g_web.config.export_config(json, sizeof(json), g_web.config.callback_ctx) != ESP_OK) {
        return web_reply_text(r, "500 Internal Server Error", "config export failed");
    }
    return web_reply_json(r, json);
}

esp_err_t web_api_config_import(web_request_t *r)
{
    if (g_web.config.import_config == NULL) {
        return web_reply_error_close(r, "501 Not Implemented", "config import unavailable");
    }
    char body[API_CONFIG_MAX];
    if (read_body(r->req, body, sizeof(body)) != ESP_OK) {
        return web_reply_error_close(r, "400 Bad Request", "request body unreadable");
    }

    const esp_err_t err = g_web.config.import_config(body, g_web.config.callback_ctx);
    if (err == ESP_ERR_INVALID_ARG) {
        event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "config import rejected");
        return web_reply_text(r, "400 Bad Request", "import rejected: invalid document, schema, checksum or value; nothing saved");
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return web_reply_text(r, "409 Conflict", "this build cannot store settings: NVS encryption is off");
    }
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, web_now_ms(), "config import could not be saved");
        return web_reply_text(r, "500 Internal Server Error", "import failed; nothing saved");
    }
    g_web.config_reboot_pending = true;
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "config imported; reboot pending");
    return web_reply_text(r, "200 OK", "config imported; reboot to apply");
}

esp_err_t web_api_ota_status(web_request_t *r)
{
    const ota_update_status_t ota = ota_update_get_status();
    const char *state = ota.in_progress ? "uploading" : (ota.pending_reboot ? "pending reboot" : "idle");
    char json[API_JSON_MAX];
    json_writer_t w;
    json_writer_init(&w, json, sizeof(json));
    json_writer_raw(&w, "{\"state\":");
    json_writer_string(&w, state);
    json_writer_raw(&w, ",\"target\":");
    json_writer_string(&w, ota.target_label);
    json_writer_raw(&w, ",\"written\":%u,\"expected\":%u,\"max_bytes\":%u,\"last_error\":",
                    (unsigned)ota.written_bytes, (unsigned)ota.expected_bytes, (unsigned)OTA_UPDATE_MAX_IMAGE_BYTES);
    json_writer_string(&w, ota.last_error == ESP_OK ? "" : esp_err_to_name(ota.last_error));
    json_writer_raw(&w, ",\"reboot_allowed\":");
    json_writer_bool(&w, ota_update_policy_reboot_allowed(ota.pending_reboot, ota.in_progress) || g_web.config_reboot_pending);
    json_writer_raw(&w, "}");
    return reply_writer(r, &w);
}

static esp_err_t ota_fail(web_request_t *r, ota_update_session_t *session, esp_err_t reason, const char *status, const char *message)
{
    ota_update_abort(session, reason);
    event_log_append(EVENT_LOG_ERROR, web_now_ms(), message);
    return web_reply_error_close(r, status, message);
}

esp_err_t web_api_ota_upload(web_request_t *r)
{
    httpd_req_t *req = r->req;
    const ota_update_status_t status = ota_update_get_status();
    const ota_update_policy_result_t policy = ota_update_policy_evaluate(req->content_len, status.in_progress);
    if (policy != OTA_UPDATE_POLICY_ACCEPT) {
        return web_reply_error_close(r, policy == OTA_UPDATE_POLICY_REJECT_BUSY ? "409 Conflict" : "400 Bad Request",
                                     ota_update_policy_result_name(policy));
    }

    ota_update_session_t session = {0};
    if (ota_update_begin(req->content_len, &session) != ESP_OK) {
        return web_reply_error_close(r, "500 Internal Server Error", "could not open the OTA slot");
    }
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "ota upload started");

    static uint8_t buf[API_OTA_CHUNK];
    size_t received = 0;
    unsigned consecutive_timeouts = 0;
    const uint64_t start_ms = web_now_ms();
    while (received < req->content_len) {
        if (ota_update_policy_deadline_exceeded(start_ms, web_now_ms())) {
            return ota_fail(r, &session, ESP_ERR_TIMEOUT, "408 Request Timeout", "ota upload deadline exceeded");
        }
        const size_t remaining = req->content_len - received;
        const int got = httpd_req_recv(req, (char *)buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            if (ota_update_policy_timeout_exhausted(++consecutive_timeouts)) {
                return ota_fail(r, &session, ESP_ERR_TIMEOUT, "408 Request Timeout", "ota upload stalled");
            }
            continue;
        }
        if (got <= 0) {
            return ota_fail(r, &session, ESP_FAIL, "400 Bad Request", "ota upload interrupted");
        }
        consecutive_timeouts = 0;
        const esp_err_t err = ota_update_write(&session, buf, (size_t)got);
        if (err == ESP_ERR_NOT_SUPPORTED) {
            return ota_fail(r, &session, err, "415 Unsupported Media Type", "not a firmware image for this device");
        }
        if (err != ESP_OK) {
            return ota_fail(r, &session, err, "500 Internal Server Error", "ota write failed");
        }
        received += (size_t)got;
    }

    const esp_err_t err = ota_update_finish(&session);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ota image rejected: %s", esp_err_to_name(err));
        event_log_append(EVENT_LOG_ERROR, web_now_ms(), "ota image failed validation");
        return web_reply_text(r, "400 Bad Request", "image failed validation (size, checksum or signature)");
    }
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "ota image accepted; reboot pending");
    return web_reply_text(r, "200 OK", "image accepted; reboot to run it");
}

esp_err_t web_api_reboot(web_request_t *r)
{
    const ota_update_status_t ota = ota_update_get_status();
    if (!ota_update_policy_reboot_allowed(ota.pending_reboot, ota.in_progress) && !g_web.config_reboot_pending) {
        return web_reply_text(r, "409 Conflict", "nothing is waiting for a reboot");
    }
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "reboot requested from web");
    web_schedule_reboot();
    return web_reply_text(r, "200 OK", "rebooting");
}

esp_err_t web_api_rotate_credentials(web_request_t *r)
{
    if (g_web.config.rotate_credentials == NULL) {
        return web_reply_text(r, "501 Not Implemented", "rotation unavailable");
    }
    const esp_err_t err = g_web.config.rotate_credentials(g_web.config.callback_ctx);
    if (err != ESP_OK) {
        event_log_append(EVENT_LOG_ERROR, web_now_ms(), "wifi password rotation failed");
        return web_reply_text(r, "409 Conflict", err == ESP_ERR_INVALID_STATE
                                                     ? "rotation needs the device screen to show the new password"
                                                     : "rotation failed; the current password still works");
    }
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "wifi password rotated; rebooting");
    web_session_end(r);
    web_schedule_reboot();
    return web_reply_text(r, "200 OK", "WiFi password rotated; rebooting");
}

esp_err_t web_api_emergency_lock(web_request_t *r)
{
    web_set_locked(true);
    web_session_end(r);
    return web_reply_text(r, "200 OK", "locked; press PWR on the device to unlock");
}
