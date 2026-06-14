#pragma once

#include "esp_err.h"

typedef struct {
    const char *web_password;
} web_server_config_t;

esp_err_t web_server_start(const web_server_config_t *config);
esp_err_t web_server_emergency_lock(void);
