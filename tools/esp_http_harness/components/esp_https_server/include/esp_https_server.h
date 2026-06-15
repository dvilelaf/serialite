#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    httpd_config_t httpd;
    const uint8_t *servercert;
    size_t servercert_len;
    const uint8_t *prvtkey_pem;
    size_t prvtkey_len;
} httpd_ssl_config_t;

#define HTTPD_SSL_CONFIG_DEFAULT() ((httpd_ssl_config_t){ .httpd = HTTPD_DEFAULT_CONFIG() })

esp_err_t httpd_ssl_start(httpd_handle_t *handle, const httpd_ssl_config_t *config);
esp_err_t httpd_ssl_stop(httpd_handle_t handle);
