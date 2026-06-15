#pragma once

#include "esp_err.h"
#include "local_tls_identity.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *wifi_password;
    bool reveal_on_local_display;
    bool reboot_required;
} web_server_credential_rotation_t;

typedef esp_err_t (*web_server_rotate_credentials_fn_t)(
    const web_server_credential_rotation_t *rotation,
    void *ctx);

typedef esp_err_t (*web_server_export_config_fn_t)(char *out, size_t out_size, void *ctx);
typedef esp_err_t (*web_server_import_config_fn_t)(const char *json, void *ctx);

typedef struct {
    const local_tls_identity_t *tls_identity;
    bool tls_fingerprint_displayed_locally;
    web_server_rotate_credentials_fn_t rotate_credentials;
    void *rotate_credentials_ctx;
    web_server_export_config_fn_t export_config;
    web_server_import_config_fn_t import_config;
    void *config_ctx;
} web_server_config_t;

typedef struct {
    bool started;
    bool writer_active;
    bool locked;
    bool tls_active;
    uint32_t ws_client_count;
} web_server_status_t;

esp_err_t web_server_start(const web_server_config_t *config);
esp_err_t web_server_emergency_lock(void);
web_server_status_t web_server_get_status(void);
