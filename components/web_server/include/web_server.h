#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    const char *web_password;
} web_server_config_t;

typedef struct {
    bool started;
    bool writer_active;
    bool locked;
} web_server_status_t;

esp_err_t web_server_start(const web_server_config_t *config);
esp_err_t web_server_emergency_lock(void);
web_server_status_t web_server_get_status(void);
