#include "config_transfer.h"

#include <stdio.h>
#include <string.h>

static uint32_t fnv1a_update(uint32_t hash, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t config_checksum(
    const char *ssid,
    unsigned channel,
    unsigned max_clients,
    unsigned brightness,
    unsigned font_size)
{
    char canonical[160];
    const int written = snprintf(
        canonical,
        sizeof(canonical),
        "%s|%s|%u|%u|%u|%u",
        CONFIG_TRANSFER_SCHEMA,
        ssid,
        channel,
        max_clients,
        brightness,
        font_size);
    if (written < 0 || written >= (int)sizeof(canonical)) {
        return 0;
    }
    return fnv1a_update(2166136261U, canonical, (size_t)written);
}

static bool ssid_export_safe(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i <= STORAGE_SSID_MAX_BYTES; ++i) {
        const char c = ssid[i];
        if (c == '\0') {
            return true;
        }
        if (c == '"' || c == '\\' || (unsigned char)c < 0x20U) {
            return false;
        }
    }
    return false;
}

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return;
    }
    size_t len = strnlen(src, dst_size - 1U);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

config_transfer_result_t config_transfer_export_json(
    const storage_config_t *config,
    char *out,
    size_t out_size)
{
    if (config == NULL || out == NULL || out_size == 0) {
        return CONFIG_TRANSFER_ERR_INVALID_ARG;
    }
    if (!ssid_export_safe(config->wifi.ssid) ||
        config->wifi.channel < 1 || config->wifi.channel > 13 ||
        config->wifi.max_clients < 1 || config->wifi.max_clients > 4) {
        return CONFIG_TRANSFER_ERR_UNSAFE_VALUE;
    }

    const uint32_t checksum = config_checksum(
        config->wifi.ssid,
        config->wifi.channel,
        config->wifi.max_clients,
        config->brightness,
        config->font_size);
    const int written = snprintf(
        out,
        out_size,
        "{\"schema\":\"%s\",\"ssid\":\"%s\",\"channel\":%u,\"max_clients\":%u,"
        "\"brightness\":%u,\"font_size\":%u,\"checksum\":\"%08x\"}",
        CONFIG_TRANSFER_SCHEMA,
        config->wifi.ssid,
        (unsigned)config->wifi.channel,
        (unsigned)config->wifi.max_clients,
        (unsigned)config->brightness,
        (unsigned)config->font_size,
        (unsigned)checksum);
    if (written < 0 || written >= (int)out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return CONFIG_TRANSFER_ERR_OUTPUT_TOO_SMALL;
    }
    return CONFIG_TRANSFER_OK;
}

static bool consume_literal(const char **cursor, const char *literal)
{
    const size_t len = strlen(literal);
    if (strncmp(*cursor, literal, len) != 0) {
        return false;
    }
    *cursor += len;
    return true;
}

static bool consume_string(const char **cursor, char *out, size_t out_size)
{
    if (!consume_literal(cursor, "\"")) {
        return false;
    }
    size_t len = 0;
    while ((*cursor)[len] != '\0' && (*cursor)[len] != '"') {
        if ((*cursor)[len] == '\\' || (unsigned char)(*cursor)[len] < 0x20U || len + 1U >= out_size) {
            return false;
        }
        len++;
    }
    if ((*cursor)[len] != '"') {
        return false;
    }
    memcpy(out, *cursor, len);
    out[len] = '\0';
    *cursor += len + 1U;
    return true;
}

static bool consume_uint(const char **cursor, unsigned *out)
{
    if ((*cursor)[0] == '0' && (*cursor)[1] >= '0' && (*cursor)[1] <= '9') {
        return false;
    }
    unsigned value = 0;
    size_t digits = 0;
    while ((*cursor)[digits] >= '0' && (*cursor)[digits] <= '9') {
        value = (value * 10U) + (unsigned)((*cursor)[digits] - '0');
        digits++;
        if (value > 255U) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    *cursor += digits;
    *out = value;
    return true;
}

static bool parse_hex_u32(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        const char c = text[i];
        unsigned nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = 10U + (unsigned)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            nibble = 10U + (unsigned)(c - 'A');
        } else {
            return false;
        }
        value = (value << 4U) | nibble;
    }
    if (text[8] != '\0') {
        return false;
    }
    *out = value;
    return true;
}

config_transfer_result_t config_transfer_import_json(
    const char *json,
    storage_config_t *in_out_config)
{
    if (json == NULL || in_out_config == NULL) {
        return CONFIG_TRANSFER_ERR_INVALID_ARG;
    }
    if (strnlen(json, CONFIG_TRANSFER_MAX_JSON + 1U) > CONFIG_TRANSFER_MAX_JSON) {
        return CONFIG_TRANSFER_ERR_PARSE;
    }

    char schema[32];
    char ssid[STORAGE_SSID_MAX_LEN];
    char checksum_text[9];
    unsigned channel = 0;
    unsigned max_clients = 0;
    unsigned brightness = 0;
    unsigned font_size = 0;
    const char *cursor = json;
    if (!consume_literal(&cursor, "{\"schema\":") ||
        !consume_string(&cursor, schema, sizeof(schema)) ||
        !consume_literal(&cursor, ",\"ssid\":") ||
        !consume_string(&cursor, ssid, sizeof(ssid)) ||
        !consume_literal(&cursor, ",\"channel\":") ||
        !consume_uint(&cursor, &channel) ||
        !consume_literal(&cursor, ",\"max_clients\":") ||
        !consume_uint(&cursor, &max_clients) ||
        !consume_literal(&cursor, ",\"brightness\":") ||
        !consume_uint(&cursor, &brightness) ||
        !consume_literal(&cursor, ",\"font_size\":") ||
        !consume_uint(&cursor, &font_size) ||
        !consume_literal(&cursor, ",\"checksum\":") ||
        !consume_string(&cursor, checksum_text, sizeof(checksum_text)) ||
        !consume_literal(&cursor, "}") ||
        cursor[0] != '\0') {
        return CONFIG_TRANSFER_ERR_PARSE;
    }
    if (strcmp(schema, CONFIG_TRANSFER_SCHEMA) != 0) {
        return CONFIG_TRANSFER_ERR_SCHEMA;
    }
    if (!ssid_export_safe(ssid) ||
        channel < 1 || channel > 13 ||
        max_clients < 1 || max_clients > 4) {
        return CONFIG_TRANSFER_ERR_UNSAFE_VALUE;
    }

    uint32_t expected = 0;
    if (!parse_hex_u32(checksum_text, &expected)) {
        return CONFIG_TRANSFER_ERR_CHECKSUM;
    }
    const uint32_t actual = config_checksum(ssid, channel, max_clients, brightness, font_size);
    if (actual != expected) {
        return CONFIG_TRANSFER_ERR_CHECKSUM;
    }

    copy_cstr(in_out_config->wifi.ssid, sizeof(in_out_config->wifi.ssid), ssid);
    in_out_config->wifi.channel = (uint8_t)channel;
    in_out_config->wifi.max_clients = (uint8_t)max_clients;
    in_out_config->brightness = (uint8_t)brightness;
    in_out_config->font_size = (uint8_t)font_size;
    return CONFIG_TRANSFER_OK;
}

const char *config_transfer_result_name(config_transfer_result_t result)
{
    switch (result) {
        case CONFIG_TRANSFER_OK:
            return "ok";
        case CONFIG_TRANSFER_ERR_INVALID_ARG:
            return "invalid_arg";
        case CONFIG_TRANSFER_ERR_OUTPUT_TOO_SMALL:
            return "output_too_small";
        case CONFIG_TRANSFER_ERR_PARSE:
            return "parse";
        case CONFIG_TRANSFER_ERR_SCHEMA:
            return "schema";
        case CONFIG_TRANSFER_ERR_CHECKSUM:
            return "checksum";
        case CONFIG_TRANSFER_ERR_UNSAFE_VALUE:
            return "unsafe_value";
        default:
            return "unknown";
    }
}
