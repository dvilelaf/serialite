#pragma once

#include "storage_config.h"
#include <stddef.h>

#define CONFIG_TRANSFER_SCHEMA "serialite-config-v1"
#define CONFIG_TRANSFER_MAX_JSON 512U

typedef enum {
    CONFIG_TRANSFER_OK = 0,
    CONFIG_TRANSFER_ERR_INVALID_ARG,
    CONFIG_TRANSFER_ERR_OUTPUT_TOO_SMALL,
    CONFIG_TRANSFER_ERR_PARSE,
    CONFIG_TRANSFER_ERR_SCHEMA,
    CONFIG_TRANSFER_ERR_CHECKSUM,
    CONFIG_TRANSFER_ERR_UNSAFE_VALUE,
} config_transfer_result_t;

config_transfer_result_t config_transfer_export_json(
    const storage_config_t *config,
    char *out,
    size_t out_size);

config_transfer_result_t config_transfer_import_json(
    const char *json,
    storage_config_t *in_out_config);

const char *config_transfer_result_name(config_transfer_result_t result);
