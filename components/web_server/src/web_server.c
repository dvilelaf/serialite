#include "web_server.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "wifi_ap.h"
#include <string.h>

static const char *TAG = "web_server";

#define WEB_MAX_WS_CLIENTS 4
#define WEB_TX_QUEUE_DEPTH 16
#define WEB_TX_CHUNK_MAX 256

typedef struct {
    size_t len;
    uint8_t data[WEB_TX_CHUNK_MAX];
} web_tx_chunk_t;

static httpd_handle_t s_server;
static int s_ws_fds[WEB_MAX_WS_CLIENTS];
static QueueHandle_t s_tx_queue;

static void remove_ws_fd(int fd)
{
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
        }
    }
}

static esp_err_t add_ws_fd(int fd)
{
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] == fd) {
            return ESP_OK;
        }
    }
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        if (s_ws_fds[i] < 0) {
            s_ws_fds[i] = fd;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

static void web_tx_task(void *arg)
{
    (void)arg;
    web_tx_chunk_t chunk;
    while (true) {
        if (xQueueReceive(s_tx_queue, &chunk, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = chunk.data,
            .len = chunk.len,
        };

        for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
            if (s_ws_fds[i] >= 0) {
                esp_err_t err = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &frame);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "dropping websocket client fd=%d: %s", s_ws_fds[i], esp_err_to_name(err));
                    remove_ws_fd(s_ws_fds[i]);
                }
            }
        }
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
        chunk.len = len - offset;
        if (chunk.len > WEB_TX_CHUNK_MAX) {
            chunk.len = WEB_TX_CHUNK_MAX;
        }
        memcpy(chunk.data, data + offset, chunk.len);
        if (xQueueSend(s_tx_queue, &chunk, 0) != pdTRUE) {
            ESP_LOGW(TAG, "websocket tx queue full; dropped %u bytes", (unsigned)(len - offset));
            return;
        }
        offset += chunk.len;
    }
}

static esp_err_t index_handler(httpd_req_t *req)
{
    const usb_console_status_t usb = usb_console_get_status();
    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();

    char body[768];
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
        "<p>Bridge USB RX: %llu</p>"
        "<p>Bridge USB TX: %llu</p>"
        "<p><a href=\"/terminal\">Abrir terminal</a></p>"
        "</body></html>",
        wifi.started ? "activo" : "inactivo",
        wifi.ip_addr,
        wifi.connected_clients,
        usb.connected ? "conectado" : "desconectado",
        (unsigned long long)usb.bytes_received,
        (unsigned long long)usb.bytes_sent,
        (unsigned long long)bridge.bytes_from_usb,
        (unsigned long long)bridge.bytes_to_usb);

    if (written < 0 || written >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t terminal_handler(httpd_req_t *req)
{
    static const char terminal_page[] =
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32-KVM Terminal</title><style>"
        "html,body{margin:0;height:100%;background:#07110e;color:#d8fff4;font:15px ui-monospace,SFMono-Regular,Menlo,monospace}"
        "#bar{padding:10px 12px;background:#0d211b;border-bottom:1px solid #2ee6b8}"
        "#term{white-space:pre-wrap;overflow:auto;height:calc(100% - 104px);padding:12px;box-sizing:border-box}"
        "#input{width:100%;height:44px;box-sizing:border-box;border:0;border-top:1px solid #2ee6b8;background:#020604;color:#fff;padding:10px;font:inherit}"
        "button{margin-right:6px;background:#14392d;color:#d8fff4;border:1px solid #2ee6b8;border-radius:6px;padding:6px 9px}"
        "</style></head><body><div id=\"bar\">ESP32-KVM <span id=\"state\">conectando</span><br>"
        "<button data-k=\"\\u0003\">Ctrl+C</button><button data-k=\"\\u0004\">Ctrl+D</button>"
        "<button data-k=\"\\r\">Enter</button><button data-k=\"\\u001b\">Esc</button><button data-k=\"\\t\">Tab</button>"
        "</div><div id=\"term\"></div><input id=\"input\" autocomplete=\"off\" autocapitalize=\"none\" spellcheck=\"false\" autofocus>"
        "<script>"
        "const term=document.getElementById('term'),input=document.getElementById('input'),state=document.getElementById('state');"
        "let ws,backoff=500;function add(t){term.textContent+=t;term.scrollTop=term.scrollHeight;if(term.textContent.length>65536)term.textContent=term.textContent.slice(-49152)}"
        "function connect(){state.textContent='conectando';ws=new WebSocket(`ws://${location.host}/ws`);ws.binaryType='arraybuffer';"
        "ws.onopen=()=>{state.textContent='conectado';backoff=500;add('\\r\\n[web conectado]\\r\\n')};"
        "ws.onmessage=e=>{if(e.data instanceof ArrayBuffer)add(new TextDecoder().decode(e.data));else add(e.data)};"
        "ws.onclose=()=>{state.textContent='reconectando';setTimeout(connect,backoff);backoff=Math.min(backoff*2,5000)};"
        "ws.onerror=()=>ws.close()}"
        "function send(s){if(ws&&ws.readyState===1)ws.send(s)}"
        "input.addEventListener('keydown',e=>{if(e.key==='Enter'){send(input.value+'\\r');input.value='';e.preventDefault()}});"
        "document.querySelectorAll('button').forEach(b=>b.onclick=()=>send(b.dataset.k));connect();"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, terminal_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        const int fd = httpd_req_to_sockfd(req);
        if (add_ws_fd(fd) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "too many websocket clients");
        }
        ESP_LOGI(TAG, "websocket client connected fd=%d", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = NULL,
        .len = 0,
    };
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, 0), TAG, "ws frame header failed");
    if (frame.len > 512) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "websocket frame too large");
    }

    uint8_t payload[512];
    frame.payload = payload;
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, frame.len), TAG, "ws frame payload failed");
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_ws_fd(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_BINARY && frame.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }

    const size_t written = terminal_bridge_submit_input(TERMINAL_BRIDGE_SOURCE_WEB, payload, frame.len);
    if (written < frame.len) {
        ESP_LOGW(TAG, "bridge input queue full; accepted %u/%u bytes", (unsigned)written, (unsigned)frame.len);
    }
    return ESP_OK;
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
    s_server = server;
    for (size_t i = 0; i < WEB_MAX_WS_CLIENTS; ++i) {
        s_ws_fds[i] = -1;
    }
    s_tx_queue = xQueueCreate(WEB_TX_QUEUE_DEPTH, sizeof(web_tx_chunk_t));
    if (s_tx_queue == NULL) {
        httpd_stop(server);
        return ESP_ERR_NO_MEM;
    }
    BaseType_t task_ok = xTaskCreate(web_tx_task, "web_tx", 4096, NULL, 3, NULL);
    if (task_ok != pdPASS) {
        httpd_stop(server);
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(terminal_bridge_register_output_callback(bridge_output_cb, NULL), TAG, "bridge callback failed");

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
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &index_uri), fail, TAG, "index handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &terminal_uri), fail, TAG, "terminal handler failed");
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(server, &ws_uri), fail, TAG, "websocket handler failed");

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;

fail:
    httpd_stop(server);
    return ret;
}
