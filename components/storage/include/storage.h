#pragma once

#include "esp_err.h"
#include "storage_config.h"

esp_err_t storage_init(void);
esp_err_t storage_load_config(storage_config_t *config);
esp_err_t storage_save_config(const storage_config_t *config);
esp_err_t storage_factory_reset(void);
