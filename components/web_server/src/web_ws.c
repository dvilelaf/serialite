/*
 * Terminal WebSocket. Every function here runs in the httpd task except
 * bridge_output_cb, which only filters and queues output and then asks the
 * httpd task to send it, so sockets are never written from two tasks.
 */

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "terminal_bridge.h"
#include "web_input_policy.h"
#include "web_internal.h"
#include "web_terminal_ansi.h"

static const char *TAG = "web_ws";

#define WEB_MAX_WS_CLIENTS 2
#define WEB_TX_CHUNK_MAX 256
/* Room for an escape sequence carried over from the previous chunk. */
#define WEB_TX_INPUT_MAX (WEB_TX_CHUNK_MAX - WEB_TERMINAL_ANSI_MAX_CARRY)
#define WEB_TX_QUEUE_DEPTH 16
#define WEB_TX_CHUNKS_PER_WORK 8
#define WEB_SCROLLBACK_MAX 1024

typedef struct {
    int fd;
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
} web_ws_client_t;

typedef struct {
    size_t len;
    uint8_t data[WEB_TX_CHUNK_MAX];
} web_tx_chunk_t;

static web_ws_client_t s_clients[WEB_MAX_WS_CLIENTS];
static web_input_policy_state_t s_input_policy;
static web_terminal_ansi_state_t s_output_ansi;
static QueueHandle_t s_tx_queue;
static atomic_bool s_tx_scheduled;

static void publish_count(void)
{
    uint32_t count = 0;
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        count += s_clients[i].fd >= 0 ? 1U : 0U;
    }
    web_status_set_ws_count(count);
}

static web_ws_client_t *find_client(int fd)
{
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].fd == fd) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static void forget_client(web_ws_client_t *client)
{
    client->fd = -1;
    memset(client->session_token, 0, sizeof(client->session_token));
    publish_count();
}

static void drop_client(web_ws_client_t *client)
{
    const int fd = client->fd;
    forget_client(client);
    (void)httpd_sess_trigger_close(g_web.server, fd);
}

void web_ws_init(void)
{
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        s_clients[i].fd = -1;
        memset(s_clients[i].session_token, 0, sizeof(s_clients[i].session_token));
    }
    web_input_policy_init(&s_input_policy);
    web_terminal_ansi_init(&s_output_ansi);
    atomic_store(&s_tx_scheduled, false);
    publish_count();
}

void web_ws_on_socket_close(int fd)
{
    web_ws_client_t *client = find_client(fd);
    if (client != NULL) {
        forget_client(client);
        event_log_append(EVENT_LOG_INFO, web_now_ms(), "terminal client disconnected");
    }
}

void web_ws_close_all(void)
{
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].fd >= 0) {
            drop_client(&s_clients[i]);
        }
    }
}

static esp_err_t send_binary(int fd, const uint8_t *data, size_t len)
{
    if (httpd_ws_get_fd_info(g_web.server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = (uint8_t *)data,
        .len = len,
    };
    return httpd_ws_send_frame_async(g_web.server, fd, &frame);
}

static void send_scrollback(web_ws_client_t *client)
{
    uint8_t raw[WEB_SCROLLBACK_MAX];
    uint8_t filtered[WEB_SCROLLBACK_MAX + WEB_TERMINAL_ANSI_MAX_CARRY];
    const size_t raw_len = terminal_bridge_snapshot_recent_output(raw, sizeof(raw));
    web_terminal_ansi_state_t ansi;
    web_terminal_ansi_init(&ansi);
    const size_t len = web_terminal_ansi_filter(&ansi, raw, raw_len, filtered, sizeof(filtered));
    for (size_t offset = 0; offset < len; offset += WEB_TX_CHUNK_MAX) {
        const size_t chunk = len - offset < WEB_TX_CHUNK_MAX ? len - offset : WEB_TX_CHUNK_MAX;
        if (send_binary(client->fd, filtered + offset, chunk) != ESP_OK) {
            ESP_LOGW(TAG, "scrollback send failed fd=%d", client->fd);
            drop_client(client);
            return;
        }
    }
}

/* ---- output path -------------------------------------------------------- */

static void tx_work(void *arg)
{
    (void)arg;
    atomic_store(&s_tx_scheduled, false);

    web_tx_chunk_t chunk;
    for (unsigned n = 0; n < WEB_TX_CHUNKS_PER_WORK && xQueueReceive(s_tx_queue, &chunk, 0) == pdTRUE; ++n) {
        for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
            if (s_clients[i].fd >= 0 && send_binary(s_clients[i].fd, chunk.data, chunk.len) != ESP_OK) {
                ESP_LOGW(TAG, "dropping terminal client fd=%d", s_clients[i].fd);
                event_log_append(EVENT_LOG_WARN, web_now_ms(), "terminal client dropped during send");
                drop_client(&s_clients[i]);
            }
        }
    }

    /* Leave room for other httpd work; come back for the rest. */
    if (uxQueueMessagesWaiting(s_tx_queue) > 0 && !atomic_exchange(&s_tx_scheduled, true)) {
        if (httpd_queue_work(g_web.server, tx_work, NULL) != ESP_OK) {
            atomic_store(&s_tx_scheduled, false);
        }
    }
}

static void bridge_output_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    for (size_t offset = 0; offset < len;) {
        const size_t take = len - offset < WEB_TX_INPUT_MAX ? len - offset : WEB_TX_INPUT_MAX;
        web_tx_chunk_t chunk;
        chunk.len = web_terminal_ansi_filter(&s_output_ansi, data + offset, take, chunk.data, sizeof(chunk.data));
        offset += take;
        if (chunk.len > 0 && xQueueSend(s_tx_queue, &chunk, 0) != pdTRUE) {
            event_log_append(EVENT_LOG_WARN, web_now_ms(), "terminal output queue full; output dropped");
            break;
        }
    }

    if (!atomic_exchange(&s_tx_scheduled, true)) {
        if (g_web.server == NULL || httpd_queue_work(g_web.server, tx_work, NULL) != ESP_OK) {
            atomic_store(&s_tx_scheduled, false);
        }
    }
}

esp_err_t web_ws_attach_bridge(void)
{
    if (s_tx_queue == NULL) {
        s_tx_queue = xQueueCreate(WEB_TX_QUEUE_DEPTH, sizeof(web_tx_chunk_t));
        if (s_tx_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return terminal_bridge_register_output_callback(bridge_output_cb, NULL);
}

/* ---- handler ------------------------------------------------------------ */

static esp_err_t accept_handshake(httpd_req_t *req)
{
    char host[WEB_HEADER_VALUE_MAX] = {0};
    char origin[WEB_HEADER_VALUE_MAX] = {0};
    char cookie[WEB_COOKIE_HEADER_MAX] = {0};
    char token[WEB_SECURITY_TOKEN_BUF_LEN] = {0};
    const uint64_t now = web_now_ms();

    const web_guard_rule_t rule = {.access = WEB_ACCESS_SESSION_ORIGIN, .blocked_when_locked = true};
    const web_guard_facts_t facts = {
        .rate_limited = http_rate_limit_evaluate(&g_web.rate_limit, now) != HTTP_RATE_LIMIT_ACCEPT,
        .host_allowed = httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK &&
                        web_request_host_allowed(&g_web.site, host),
        .origin_allowed = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK &&
                          web_request_origin_allowed(&g_web.site, origin),
        .session_valid = httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK &&
                         web_request_cookie_value(cookie, WEB_SESSION_COOKIE, token, sizeof(token)) &&
                         web_security_touch(&g_web.security, token, now),
        .locked = g_web.locked,
    };
    const web_guard_result_t verdict = web_guard_evaluate(&rule, &facts);
    if (verdict != WEB_GUARD_ALLOW) {
        char message[EVENT_LOG_MESSAGE_MAX];
        snprintf(message, sizeof(message), "terminal connection rejected: %s", web_guard_result_name(verdict));
        event_log_append(EVENT_LOG_SECURITY, now, message);
        /* The 101 response is already out; failing closes the socket. */
        return ESP_FAIL;
    }

    const int fd = httpd_req_to_sockfd(req);
    web_ws_client_t *client = find_client(fd);
    if (client == NULL) {
        client = find_client(-1);
    }
    if (client == NULL) {
        event_log_append(EVENT_LOG_WARN, now, "terminal connection rejected: too many clients");
        return ESP_FAIL;
    }
    client->fd = fd;
    strlcpy(client->session_token, token, sizeof(client->session_token));
    publish_count();
    event_log_append(EVENT_LOG_INFO, now, "terminal client connected");
    send_scrollback(client);
    return ESP_OK;
}

static esp_err_t receive_input(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);
    web_ws_client_t *client = find_client(fd);
    const uint64_t now = web_now_ms();
    if (client == NULL || g_web.locked || !web_security_touch(&g_web.security, client->session_token, now)) {
        if (client != NULL) {
            forget_client(client);
        }
        return ESP_FAIL;
    }

    httpd_ws_frame_t frame = {0};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.len > WEB_INPUT_POLICY_FRAME_MAX) {
        event_log_append(EVENT_LOG_SECURITY, now, "terminal input rejected: frame too large");
        return ESP_FAIL;
    }
    uint8_t payload[WEB_INPUT_POLICY_FRAME_MAX];
    frame.payload = payload;
    if (frame.len > 0 && httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT && frame.type != HTTPD_WS_TYPE_BINARY) {
        return ESP_OK;
    }

    const web_input_policy_result_t policy = web_input_policy_evaluate(&s_input_policy, frame.len, now);
    if (policy != WEB_INPUT_POLICY_ACCEPT) {
        event_log_append(EVENT_LOG_SECURITY, now, "terminal input dropped by rate policy");
        return ESP_OK;
    }
    const size_t written = terminal_bridge_submit_input(payload, frame.len);
    if (written < frame.len) {
        event_log_append(EVENT_LOG_WARN, now, "serial input queue full");
    }
    return ESP_OK;
}

esp_err_t web_ws_handler(httpd_req_t *req)
{
    return req->method == HTTP_GET ? accept_handshake(req) : receive_input(req);
}
