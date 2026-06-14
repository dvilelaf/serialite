#pragma once

#include "esp_err.h"
#include "web_password_hash.h"
#include <stdbool.h>

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

typedef struct {
    const char *web_password;
    const uint8_t *web_password_salt;
    const uint8_t *web_password_hash;
    bool web_password_hash_configured;
    web_server_rotate_credentials_fn_t rotate_credentials;
    void *rotate_credentials_ctx;
} web_server_config_t;

typedef struct {
    bool started;
    bool writer_active;
    bool locked;
} web_server_status_t;

esp_err_t web_server_start(const web_server_config_t *config);
esp_err_t web_server_emergency_lock(void);
web_server_status_t web_server_get_status(void);
