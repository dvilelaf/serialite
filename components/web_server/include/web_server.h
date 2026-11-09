#pragma once

/*
 * Local web console: static pages, a small JSON API and the terminal
 * WebSocket. All request handling, session state and WebSocket sends happen
 * in the httpd task; the public functions below are safe from any task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "local_tls_identity.h"

typedef struct {
    /* Serve HTTPS with this identity; NULL serves plain HTTP. */
    const local_tls_identity_t *tls_identity;
    /* Host names and addresses clients use; other Host/Origin values are rejected. Must outlive the server. */
    const char *const *allowed_hosts;
    size_t allowed_host_count;
    /* 0 picks 80 or 443. */
    uint16_t port;
    /* Generates, stores and displays a new WiFi password. The server reboots afterwards. */
    esp_err_t (*rotate_credentials)(void *ctx);
    esp_err_t (*export_config)(char *out, size_t out_size, void *ctx);
    /* ESP_ERR_INVALID_ARG for a rejected document, anything else for a storage failure. */
    esp_err_t (*import_config)(const char *json, void *ctx);
    void *callback_ctx;
} web_server_config_t;

typedef struct {
    bool started;
    bool tls_active;
    bool locked;
    uint32_t ws_client_count;
} web_server_status_t;

esp_err_t web_server_start(const web_server_config_t *config);
/*
 * Emergency lock: ends the session, closes terminal connections and refuses
 * new sessions until unlocked from the device.
 */
esp_err_t web_server_toggle_lock(void);
/* Closes terminal connections, e.g. after the last WiFi client left. */
esp_err_t web_server_close_clients(void);
web_server_status_t web_server_get_status(void);
