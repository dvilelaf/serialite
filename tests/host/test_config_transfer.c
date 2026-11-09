#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_transfer.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static storage_config_t sample_config(void)
{
    storage_config_t config = {0};
    strcpy(config.wifi.ssid, "Serialite");
    strcpy(config.wifi.password, "secret-password-that-must-not-export");
    config.wifi.channel = 6;
    return config;
}

static void test_export_omits_secrets_and_includes_checksum(void)
{
    const storage_config_t config = sample_config();
    char json[CONFIG_TRANSFER_MAX_JSON];

    CHECK(config_transfer_export_json(&config, json, sizeof(json)) == CONFIG_TRANSFER_OK);
    CHECK(strstr(json, CONFIG_TRANSFER_SCHEMA) != NULL);
    CHECK(strstr(json, "\"checksum\":\"") != NULL);
    CHECK(strstr(json, "secret-password") == NULL);
    CHECK(strstr(json, "password") == NULL);
}

static void test_import_round_trip_preserves_secrets(void)
{
    const storage_config_t exported = sample_config();
    storage_config_t imported = sample_config();
    strcpy(imported.wifi.ssid, "OLD");
    strcpy(imported.wifi.password, "existing-secret-password");
    imported.wifi.channel = 1;
    char json[CONFIG_TRANSFER_MAX_JSON];

    CHECK(config_transfer_export_json(&exported, json, sizeof(json)) == CONFIG_TRANSFER_OK);
    CHECK(config_transfer_import_json(json, &imported) == CONFIG_TRANSFER_OK);
    CHECK(strcmp(imported.wifi.ssid, "Serialite") == 0);
    CHECK(strcmp(imported.wifi.password, "existing-secret-password") == 0);
    CHECK(imported.wifi.channel == 6);
}

static void test_import_rejects_tampering_schema_and_unsafe_values(void)
{
    storage_config_t config = sample_config();
    char json[CONFIG_TRANSFER_MAX_JSON];

    CHECK(config_transfer_export_json(&config, json, sizeof(json)) == CONFIG_TRANSFER_OK);
    char tampered[CONFIG_TRANSFER_MAX_JSON * 2U];
    strcpy(tampered, json);
    char *channel = strstr(tampered, "\"channel\":6");
    CHECK(channel != NULL);
    channel[strlen("\"channel\":")] = '7';
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_CHECKSUM);

    strcpy(tampered, json);
    char *schema = strstr(tampered, CONFIG_TRANSFER_SCHEMA);
    CHECK(schema != NULL);
    schema[0] = 'x';
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_SCHEMA);

    storage_config_t unsafe = sample_config();
    strcpy(unsafe.wifi.ssid, "bad\"ssid");
    CHECK(config_transfer_export_json(&unsafe, json, sizeof(json)) == CONFIG_TRANSFER_ERR_UNSAFE_VALUE);
}

static void test_import_rejects_noncanonical_or_secret_bearing_json(void)
{
    storage_config_t config = sample_config();
    storage_config_t before = config;
    char json[CONFIG_TRANSFER_MAX_JSON];
    char tampered[CONFIG_TRANSFER_MAX_JSON * 2U];

    CHECK(config_transfer_export_json(&config, json, sizeof(json)) == CONFIG_TRANSFER_OK);

    snprintf(tampered, sizeof(tampered), "%s ", json);
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_PARSE);

    snprintf(tampered, sizeof(tampered), "%s,\"password\":\"secret\"", json);
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_PARSE);

    strcpy(tampered, json);
    char *channel = strstr(tampered, "\"channel\":6");
    CHECK(channel != NULL);
    channel[strlen("\"channel\":6")] = 'x';
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_PARSE);

    strcpy(tampered, json);
    channel = strstr(tampered, "\"channel\":6");
    CHECK(channel != NULL);
    memmove(channel + strlen("\"channel\":0"), channel + strlen("\"channel\":"), strlen(channel + strlen("\"channel\":")) + 1U);
    channel[strlen("\"channel\":")] = '0';
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_PARSE);

    snprintf(tampered, sizeof(tampered), "{\"wrapper\":%s}", json);
    CHECK(config_transfer_import_json(tampered, &config) == CONFIG_TRANSFER_ERR_PARSE);

    CHECK(strcmp(config.wifi.password, before.wifi.password) == 0);
    CHECK(strcmp(config.wifi.ssid, before.wifi.ssid) == 0);
}

static void test_import_rejects_out_of_range_channel(void)
{
    storage_config_t config = sample_config();
    const char *json = "{\"schema\":\"" CONFIG_TRANSFER_SCHEMA "\",\"ssid\":\"Serialite\",\"channel\":14,\"checksum\":\"00000000\"}";
    CHECK(config_transfer_import_json(json, &config) == CONFIG_TRANSFER_ERR_UNSAFE_VALUE);
    CHECK(config.wifi.channel == 6);
}

int main(void)
{
    test_export_omits_secrets_and_includes_checksum();
    test_import_round_trip_preserves_secrets();
    test_import_rejects_tampering_schema_and_unsafe_values();
    test_import_rejects_noncanonical_or_secret_bearing_json();
    test_import_rejects_out_of_range_channel();
    return 0;
}
