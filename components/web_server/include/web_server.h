#pragma once

#include "esp_err.h"
#include "web_password_hash.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *wifi_password;
    const char *web_password;
    const uint8_t *web_password_salt;
    const uint8_t *web_password_hash;
    bool reveal_on_local_display;
    bool reboot_required;
} web_server_credential_rotation_t;

typedef esp_err_t (*web_server_rotate_credentials_fn_t)(
    const web_server_credential_rotation_t *rotation,
    void *ctx);

typedef enum {
    WEB_SERVER_PAIRING_CONSUMED = 0,
    WEB_SERVER_PAIRING_LOCKED,
} web_server_pairing_event_t;

typedef void (*web_server_pairing_event_fn_t)(web_server_pairing_event_t event, void *ctx);
typedef esp_err_t (*web_server_export_config_fn_t)(char *out, size_t out_size, void *ctx);
typedef esp_err_t (*web_server_import_config_fn_t)(const char *json, void *ctx);

typedef struct {
    const char *web_password;
    const char *pairing_code;
    const uint8_t *web_password_salt;
    const uint8_t *web_password_hash;
    bool web_password_hash_configured;
    web_server_rotate_credentials_fn_t rotate_credentials;
    void *rotate_credentials_ctx;
    web_server_pairing_event_fn_t pairing_event;
    void *pairing_event_ctx;
    web_server_export_config_fn_t export_config;
    web_server_import_config_fn_t import_config;
    void *config_ctx;
} web_server_config_t;

typedef struct {
    bool started;
    bool writer_active;
    bool locked;
} web_server_status_t;

esp_err_t web_server_start(const web_server_config_t *config);
esp_err_t web_server_emergency_lock(void);
web_server_status_t web_server_get_status(void);
