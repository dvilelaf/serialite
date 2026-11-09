#pragma once

/* Shared between the web_server source files. Everything here runs in the httpd task. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_http_server.h"
#include "http_rate_limit.h"
#include "web_guard.h"
#include "web_request.h"
#include "web_security.h"
#include "web_server.h"

#define WEB_SESSION_COOKIE "serialite_session"
#define WEB_HEADER_VALUE_MAX 160
#define WEB_COOKIE_MAX 112
#define WEB_COOKIE_HEADER_MAX 512
#define WEB_CSP_MAX 320
#define WEB_ETAG_MAX 24

/* Per-request scratch; header values set on the response point into it. */
typedef struct {
    httpd_req_t *req;
    /* The caller's session token, or empty when it has none. */
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char set_cookie[WEB_COOKIE_MAX];
    char csp[WEB_CSP_MAX];
} web_request_t;

typedef struct {
    httpd_handle_t server;
    web_server_config_t config;
    web_request_site_t site;
    web_security_state_t security;
    http_rate_limit_state_t rate_limit;
    bool locked;
    bool config_reboot_pending;
    char etag[WEB_ETAG_MAX];
} web_state_t;

extern web_state_t g_web;

uint64_t web_now_ms(void);
bool web_random(uint8_t *buf, size_t len, void *ctx);

/* Replies; each returns what the handler should return. */
esp_err_t web_reply_text(web_request_t *r, const char *status, const char *text);
esp_err_t web_reply_json(web_request_t *r, const char *json);
/* Replies and asks httpd to close the connection, for errors with request body left unread. */
esp_err_t web_reply_error_close(web_request_t *r, const char *status, const char *text);

/* Starts a fresh session for this request and sets its cookie. */
bool web_session_start(web_request_t *r);
void web_session_end(web_request_t *r);
void web_set_locked(bool locked);
void web_schedule_reboot(void);
void web_status_set_ws_count(uint32_t count);

void web_ws_init(void);
esp_err_t web_ws_handler(httpd_req_t *req);
void web_ws_on_socket_close(int fd);
void web_ws_close_all(void);
esp_err_t web_ws_attach_bridge(void);

esp_err_t web_api_session(web_request_t *r);
esp_err_t web_api_logout(web_request_t *r);
esp_err_t web_api_status(web_request_t *r);
esp_err_t web_api_about(web_request_t *r);
esp_err_t web_api_diagnostics(web_request_t *r);
esp_err_t web_api_config_export(web_request_t *r);
esp_err_t web_api_config_import(web_request_t *r);
esp_err_t web_api_ota_status(web_request_t *r);
esp_err_t web_api_ota_upload(web_request_t *r);
esp_err_t web_api_reboot(web_request_t *r);
esp_err_t web_api_rotate_credentials(web_request_t *r);
esp_err_t web_api_emergency_lock(web_request_t *r);
