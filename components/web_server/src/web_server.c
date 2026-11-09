#include "web_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_rate_limit.h"
#include "ota_update_policy.h"
#include "web_internal.h"

static const char *TAG = "web_server";

#define WEB_HTTPD_STACK_SIZE (16 * 1024)
#define WEB_HTTPD_MAX_URI_HANDLERS 32
#define WEB_HTTPD_MAX_SOCKETS 6
#define WEB_REBOOT_DELAY_MS 750
#define WEB_CONFIG_BODY_MAX 512U

web_state_t g_web;

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static web_server_status_t s_status;

/* ---- static assets ------------------------------------------------------ */

#define DECLARE_ASSET(sym)                                                    \
    extern const uint8_t _binary_##sym##_start[] asm("_binary_" #sym "_start"); \
    extern const uint8_t _binary_##sym##_end[] asm("_binary_" #sym "_end")

DECLARE_ASSET(terminal_html);
DECLARE_ASSET(about_html);
DECLARE_ASSET(config_html);
DECLARE_ASSET(ota_html);
DECLARE_ASSET(app_css);
DECLARE_ASSET(terminal_css);
DECLARE_ASSET(common_js);
DECLARE_ASSET(terminal_js);
DECLARE_ASSET(about_js);
DECLARE_ASSET(config_js);
DECLARE_ASSET(ota_js);
DECLARE_ASSET(xterm_js);
DECLARE_ASSET(xterm_css);

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char *content_type;
} web_asset_t;

#define ASSET(sym, type) &(const web_asset_t){_binary_##sym##_start, _binary_##sym##_end, type}
#define HTML "text/html; charset=utf-8"
#define CSS "text/css; charset=utf-8"
#define JS "application/javascript; charset=utf-8"

/* ---- routes ------------------------------------------------------------- */

typedef esp_err_t (*web_handler_t)(web_request_t *r);

typedef struct {
    const char *uri;
    httpd_method_t method;
    web_guard_rule_t rule;
    web_handler_t handler;
    const web_asset_t *asset;
} web_route_t;

static esp_err_t serve_asset(web_request_t *r);
static esp_err_t redirect_to_terminal(web_request_t *r);

#define PUBLIC {.access = WEB_ACCESS_PUBLIC}
#define READ {.access = WEB_ACCESS_SESSION}
#define WRITE(max_body) {.access = WEB_ACCESS_SESSION_CSRF, .max_body_len = (max_body), .blocked_when_locked = true}

static const web_route_t ROUTES[] = {
    {"/", HTTP_GET, PUBLIC, redirect_to_terminal, NULL},
    {"/terminal", HTTP_GET, PUBLIC, serve_asset, ASSET(terminal_html, HTML)},
    {"/about", HTTP_GET, PUBLIC, serve_asset, ASSET(about_html, HTML)},
    {"/config", HTTP_GET, PUBLIC, serve_asset, ASSET(config_html, HTML)},
    {"/ota", HTTP_GET, PUBLIC, serve_asset, ASSET(ota_html, HTML)},
    {"/assets/app.css", HTTP_GET, PUBLIC, serve_asset, ASSET(app_css, CSS)},
    {"/assets/terminal.css", HTTP_GET, PUBLIC, serve_asset, ASSET(terminal_css, CSS)},
    {"/assets/common.js", HTTP_GET, PUBLIC, serve_asset, ASSET(common_js, JS)},
    {"/assets/terminal.js", HTTP_GET, PUBLIC, serve_asset, ASSET(terminal_js, JS)},
    {"/assets/about.js", HTTP_GET, PUBLIC, serve_asset, ASSET(about_js, JS)},
    {"/assets/config.js", HTTP_GET, PUBLIC, serve_asset, ASSET(config_js, JS)},
    {"/assets/ota.js", HTTP_GET, PUBLIC, serve_asset, ASSET(ota_js, JS)},
    {"/assets/xterm.js", HTTP_GET, PUBLIC, serve_asset, ASSET(xterm_js, JS)},
    {"/assets/xterm.css", HTTP_GET, PUBLIC, serve_asset, ASSET(xterm_css, CSS)},
    {"/api/session", HTTP_POST, {.access = WEB_ACCESS_ORIGIN, .blocked_when_locked = true}, web_api_session, NULL},
    {"/api/logout", HTTP_POST, WRITE(0), web_api_logout, NULL},
    {"/api/status", HTTP_GET, READ, web_api_status, NULL},
    {"/api/about", HTTP_GET, READ, web_api_about, NULL},
    {"/api/diagnostics", HTTP_GET, READ, web_api_diagnostics, NULL},
    {"/api/config", HTTP_GET, READ, web_api_config_export, NULL},
    {"/api/config", HTTP_POST, WRITE(WEB_CONFIG_BODY_MAX), web_api_config_import, NULL},
    {"/api/ota", HTTP_GET, READ, web_api_ota_status, NULL},
    {"/api/ota", HTTP_POST, WRITE(OTA_UPDATE_MAX_IMAGE_BYTES), web_api_ota_upload, NULL},
    {"/api/reboot", HTTP_POST, WRITE(0), web_api_reboot, NULL},
    {"/api/credentials/rotate", HTTP_POST, WRITE(0), web_api_rotate_credentials, NULL},
    /* The lock must stay reachable to whoever holds the session, so it is not blocked when locked. */
    {"/api/emergency-lock", HTTP_POST, {.access = WEB_ACCESS_SESSION_CSRF}, web_api_emergency_lock, NULL},
};

/* ---- helpers ------------------------------------------------------------ */

uint64_t web_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

bool web_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return true;
}

static void status_update(void (*apply)(web_server_status_t *status, uint32_t value), uint32_t value)
{
    portENTER_CRITICAL(&s_status_lock);
    apply(&s_status, value);
    portEXIT_CRITICAL(&s_status_lock);
}

static void apply_ws_count(web_server_status_t *status, uint32_t value)
{
    status->ws_client_count = value;
}

static void apply_locked(web_server_status_t *status, uint32_t value)
{
    status->locked = value != 0;
}

void web_status_set_ws_count(uint32_t count)
{
    status_update(apply_ws_count, count);
}

static void set_security_headers(web_request_t *r)
{
    httpd_resp_set_hdr(r->req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r->req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(r->req, "X-Frame-Options", "DENY");
    if (r->csp[0] != '\0') {
        httpd_resp_set_hdr(r->req, "Content-Security-Policy", r->csp);
    }
    if (r->set_cookie[0] != '\0') {
        httpd_resp_set_hdr(r->req, "Set-Cookie", r->set_cookie);
    }
}

static esp_err_t send_body(web_request_t *r, const char *status, const char *type, const char *body)
{
    set_security_headers(r);
    httpd_resp_set_hdr(r->req, "Cache-Control", "no-store");
    httpd_resp_set_status(r->req, status);
    httpd_resp_set_type(r->req, type);
    return httpd_resp_send(r->req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_reply_text(web_request_t *r, const char *status, const char *text)
{
    return send_body(r, status, "text/plain; charset=utf-8", text);
}

esp_err_t web_reply_json(web_request_t *r, const char *json)
{
    return send_body(r, "200 OK", "application/json", json);
}

esp_err_t web_reply_error_close(web_request_t *r, const char *status, const char *text)
{
    httpd_resp_set_hdr(r->req, "Connection", "close");
    (void)web_reply_text(r, status, text);
    return ESP_FAIL;
}

static esp_err_t redirect_to_terminal(web_request_t *r)
{
    set_security_headers(r);
    httpd_resp_set_status(r->req, "303 See Other");
    httpd_resp_set_hdr(r->req, "Location", "/terminal");
    return httpd_resp_send(r->req, "", 0);
}

static esp_err_t serve_asset(web_request_t *r)
{
    const web_asset_t *asset = (const web_asset_t *)((const web_route_t *)r->req->user_ctx)->asset;
    char quoted_etag[WEB_ETAG_MAX + 2];
    snprintf(quoted_etag, sizeof(quoted_etag), "\"%s\"", g_web.etag);

    set_security_headers(r);
    /* Revalidate on every load so a new firmware never runs with old scripts. */
    httpd_resp_set_hdr(r->req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(r->req, "ETag", quoted_etag);

    char if_none_match[WEB_ETAG_MAX + 4];
    if (httpd_req_get_hdr_value_str(r->req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK &&
        strcmp(if_none_match, quoted_etag) == 0) {
        httpd_resp_set_status(r->req, "304 Not Modified");
        return httpd_resp_send(r->req, "", 0);
    }
    httpd_resp_set_type(r->req, asset->content_type);
    return httpd_resp_send(r->req, (const char *)asset->start, (ssize_t)(asset->end - asset->start));
}

static void build_csp(web_request_t *r, const char *host)
{
    /* host already passed the allowlist, so it is safe to echo. */
    snprintf(
        r->csp,
        sizeof(r->csp),
        "default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
        "connect-src 'self' %s://%s; base-uri 'none'; form-action 'self'; frame-ancestors 'none'",
        g_web.site.tls ? "wss" : "ws",
        host);
}

bool web_session_start(web_request_t *r)
{
    if (!web_security_start_session(&g_web.security, web_now_ms(), web_random, NULL)) {
        return false;
    }
    /* Only one operator: connections from the previous session go away. */
    web_ws_close_all();
    strlcpy(r->session_token, g_web.security.session_token, sizeof(r->session_token));
    snprintf(
        r->set_cookie,
        sizeof(r->set_cookie),
        WEB_SESSION_COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict%s",
        r->session_token,
        g_web.site.tls ? "; Secure" : "");
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), "web session started");
    return true;
}

void web_session_end(web_request_t *r)
{
    web_security_end_session(&g_web.security);
    web_ws_close_all();
    if (r != NULL) {
        r->session_token[0] = '\0';
        snprintf(r->set_cookie, sizeof(r->set_cookie), WEB_SESSION_COOKIE "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    }
}

void web_set_locked(bool locked)
{
    if (locked) {
        web_session_end(NULL);
    }
    g_web.locked = locked;
    status_update(apply_locked, locked ? 1U : 0U);
    event_log_append(EVENT_LOG_SECURITY, web_now_ms(), locked ? "emergency lock engaged" : "emergency lock released");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(WEB_REBOOT_DELAY_MS));
    esp_restart();
}

void web_schedule_reboot(void)
{
    if (xTaskCreate(reboot_task, "web_reboot", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "reboot task creation failed; restarting now");
        esp_restart();
    }
}

/* ---- dispatcher --------------------------------------------------------- */

static bool header_value(httpd_req_t *req, const char *name, char *out, size_t out_size)
{
    out[0] = '\0';
    return httpd_req_get_hdr_value_str(req, name, out, out_size) == ESP_OK;
}

/*
 * Reads every header the guard needs before anything is sent: once a response
 * starts, httpd drops the request headers.
 */
static web_guard_facts_t gather_facts(web_request_t *r, char host[WEB_HEADER_VALUE_MAX])
{
    char origin[WEB_HEADER_VALUE_MAX];
    char cookie[WEB_COOKIE_HEADER_MAX];
    char csrf[WEB_HEADER_VALUE_MAX];
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    const uint64_t now = web_now_ms();

    web_guard_facts_t facts = {
        .rate_limited = http_rate_limit_evaluate(&g_web.rate_limit, now) != HTTP_RATE_LIMIT_ACCEPT,
        .host_allowed = header_value(r->req, "Host", host, WEB_HEADER_VALUE_MAX) &&
                        web_request_host_allowed(&g_web.site, host),
        .origin_allowed = header_value(r->req, "Origin", origin, sizeof(origin)) &&
                          web_request_origin_allowed(&g_web.site, origin),
        .locked = g_web.locked,
        .body_len = r->req->content_len,
    };
    if (header_value(r->req, "Cookie", cookie, sizeof(cookie)) &&
        web_request_cookie_value(cookie, WEB_SESSION_COOKIE, token, sizeof(token)) &&
        web_security_touch(&g_web.security, token, now)) {
        facts.session_valid = true;
        strlcpy(r->session_token, token, sizeof(r->session_token));
        facts.csrf_valid = header_value(r->req, "X-CSRF-Token", csrf, sizeof(csrf)) &&
                           web_security_csrf_valid(&g_web.security, token, csrf, now);
    }
    return facts;
}

static esp_err_t dispatch(httpd_req_t *req)
{
    const web_route_t *route = (const web_route_t *)req->user_ctx;
    web_request_t r = {.req = req};
    char host[WEB_HEADER_VALUE_MAX];
    const web_guard_facts_t facts = gather_facts(&r, host);

    const web_guard_result_t verdict = web_guard_evaluate(&route->rule, &facts);
    if (verdict != WEB_GUARD_ALLOW) {
        char message[EVENT_LOG_MESSAGE_MAX];
        snprintf(message, sizeof(message), "%s rejected: %s", route->uri, web_guard_result_name(verdict));
        event_log_append(verdict == WEB_GUARD_REJECT_RATE_LIMIT ? EVENT_LOG_WARN : EVENT_LOG_SECURITY, web_now_ms(), message);
        r.session_token[0] = '\0';
        /* Close instead of draining a body the client was not allowed to send. */
        if (req->content_len > 0) {
            return web_reply_error_close(&r, web_guard_status(verdict), web_guard_result_name(verdict));
        }
        return web_reply_text(&r, web_guard_status(verdict), web_guard_result_name(verdict));
    }

    build_csp(&r, host);
    return route->handler(&r);
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t error)
{
    web_request_t r = {.req = req};
    const bool method = error == HTTPD_405_METHOD_NOT_ALLOWED;
    return web_reply_text(&r, method ? "405 Method Not Allowed" : "404 Not Found", method ? "method not allowed" : "not found");
}

static void on_socket_close(httpd_handle_t server, int fd)
{
    (void)server;
    web_ws_on_socket_close(fd);
    close(fd);
}

/* ---- lifecycle ---------------------------------------------------------- */

static esp_err_t start_httpd(const web_server_config_t *config)
{
    if (config->tls_identity != NULL) {
        httpd_ssl_config_t https = HTTPD_SSL_CONFIG_DEFAULT();
        https.httpd.stack_size = WEB_HTTPD_STACK_SIZE;
        https.httpd.max_uri_handlers = WEB_HTTPD_MAX_URI_HANDLERS;
        https.httpd.max_open_sockets = WEB_HTTPD_MAX_SOCKETS;
        https.httpd.lru_purge_enable = true;
        https.httpd.close_fn = on_socket_close;
        https.port_secure = g_web.site.port;
        https.servercert = (const uint8_t *)config->tls_identity->cert_pem;
        https.servercert_len = config->tls_identity->cert_pem_len + 1U;
        https.prvtkey_pem = (const uint8_t *)config->tls_identity->key_pem;
        https.prvtkey_len = config->tls_identity->key_pem_len + 1U;
        return httpd_ssl_start(&g_web.server, &https);
    }

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.server_port = g_web.site.port;
#ifdef CONFIG_IDF_TARGET_LINUX
    http.stack_size = 64 * 1024;
#else
    http.stack_size = WEB_HTTPD_STACK_SIZE;
#endif
    http.max_uri_handlers = WEB_HTTPD_MAX_URI_HANDLERS;
    http.max_open_sockets = WEB_HTTPD_MAX_SOCKETS;
    http.lru_purge_enable = true;
    http.close_fn = on_socket_close;
    return httpd_start(&g_web.server, &http);
}

static esp_err_t register_routes(void)
{
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); ++i) {
        const httpd_uri_t uri = {
            .uri = ROUTES[i].uri,
            .method = ROUTES[i].method,
            .handler = dispatch,
            .user_ctx = (void *)&ROUTES[i],
        };
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_web.server, &uri), TAG, "register %s failed", ROUTES[i].uri);
    }
    const httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = web_ws_handler,
        .is_websocket = true,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_web.server, &ws), TAG, "register /ws failed");
    ESP_RETURN_ON_ERROR(httpd_register_err_handler(g_web.server, HTTPD_404_NOT_FOUND, not_found_handler), TAG, "404 handler failed");
    return httpd_register_err_handler(g_web.server, HTTPD_405_METHOD_NOT_ALLOWED, not_found_handler);
}

esp_err_t web_server_start(const web_server_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->allowed_hosts != NULL && config->allowed_host_count > 0,
                        ESP_ERR_INVALID_ARG, TAG, "allowed hosts are required");
    ESP_RETURN_ON_FALSE(g_web.server == NULL, ESP_ERR_INVALID_STATE, TAG, "already started");

    memset(&g_web, 0, sizeof(g_web));
    g_web.config = *config;
    g_web.site = (web_request_site_t){
        .hosts = config->allowed_hosts,
        .host_count = config->allowed_host_count,
        .tls = config->tls_identity != NULL,
        .port = config->port != 0 ? config->port : (config->tls_identity != NULL ? 443 : 80),
    };
    web_security_init(&g_web.security);
    http_rate_limit_init(&g_web.rate_limit);
    char elf_sha[WEB_ETAG_MAX];
    if (esp_app_get_elf_sha256(elf_sha, sizeof(elf_sha)) <= 0) {
        strlcpy(elf_sha, "serialite", sizeof(elf_sha));
    }
    strlcpy(g_web.etag, elf_sha, sizeof(g_web.etag));
    web_ws_init();

    esp_err_t err = start_httpd(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s server start failed: %s", g_web.site.tls ? "HTTPS" : "HTTP", esp_err_to_name(err));
        g_web.server = NULL;
        return err;
    }
    err = register_routes();
    if (err == ESP_OK) {
        err = web_ws_attach_bridge();
    }
    if (err != ESP_OK) {
        if (g_web.site.tls) {
            (void)httpd_ssl_stop(g_web.server);
        } else {
            (void)httpd_stop(g_web.server);
        }
        g_web.server = NULL;
        return err;
    }

    portENTER_CRITICAL(&s_status_lock);
    s_status = (web_server_status_t){.started = true, .tls_active = g_web.site.tls};
    portEXIT_CRITICAL(&s_status_lock);
    event_log_append(EVENT_LOG_INFO, web_now_ms(), g_web.site.tls ? "https server started" : "http server started");
    ESP_LOGI(TAG, "%s server listening on port %u", g_web.site.tls ? "HTTPS" : "HTTP", (unsigned)g_web.site.port);
    return ESP_OK;
}

static void toggle_lock_work(void *arg)
{
    (void)arg;
    web_set_locked(!g_web.locked);
}

static void close_clients_work(void *arg)
{
    (void)arg;
    web_ws_close_all();
}

esp_err_t web_server_toggle_lock(void)
{
    ESP_RETURN_ON_FALSE(g_web.server != NULL, ESP_ERR_INVALID_STATE, TAG, "server not running");
    return httpd_queue_work(g_web.server, toggle_lock_work, NULL);
}

esp_err_t web_server_close_clients(void)
{
    ESP_RETURN_ON_FALSE(g_web.server != NULL, ESP_ERR_INVALID_STATE, TAG, "server not running");
    return httpd_queue_work(g_web.server, close_clients_work, NULL);
}

web_server_status_t web_server_get_status(void)
{
    portENTER_CRITICAL(&s_status_lock);
    const web_server_status_t status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return status;
}
