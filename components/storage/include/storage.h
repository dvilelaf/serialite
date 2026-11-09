#pragma once

#include "esp_err.h"
#include "storage_config.h"

#define ESP_ERR_STORAGE_CONFIG_CORRUPT ESP_ERR_INVALID_STATE

/* Opens the namespace and removes keys left by older firmware. */
esp_err_t storage_init(void);
/*
 * Loads the persisted AP config. Without NVS encryption nothing is ever
 * persisted, so this returns defaults and the caller generates credentials.
 */
esp_err_t storage_load_config(storage_config_t *config);
/* Returns ESP_ERR_NOT_SUPPORTED when secrets cannot be stored encrypted. */
esp_err_t storage_save_config(const storage_config_t *config);
/* Erases the whole default NVS partition. The caller must restart afterwards. */
esp_err_t storage_factory_reset(void);
