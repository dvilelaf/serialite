#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage_config.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static storage_wifi_config_t valid_config(void)
{
    storage_wifi_config_t config = {
        .channel = 6,
    };
    strcpy(config.ssid, "Serialite-ABCDEF");
    strcpy(config.password, "anchor-bison-cobalt-delta");
    return config;
}

static void test_accepts_valid_wpa2_ap_config(void)
{
    storage_wifi_config_t config = valid_config();
    CHECK(storage_wifi_config_is_valid(&config));
}

static void test_rejects_empty_or_short_secrets(void)
{
    storage_wifi_config_t config = valid_config();

    config.ssid[0] = '\0';
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    strcpy(config.password, "1234567");
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    strcpy(config.password, "12345678abcdef");
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    memset(config.password, 'a', STORAGE_PRODUCTION_PASSWORD_MIN_BYTES - 1);
    config.password[STORAGE_PRODUCTION_PASSWORD_MIN_BYTES - 1] = '\0';
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    memset(config.password, 'b', STORAGE_PRODUCTION_PASSWORD_MIN_BYTES);
    config.password[STORAGE_PRODUCTION_PASSWORD_MIN_BYTES] = '\0';
    CHECK(storage_wifi_config_is_valid(&config));
}

static void test_rejects_unterminated_strings(void)
{
    storage_wifi_config_t config = valid_config();
    memset(config.ssid, 'a', sizeof(config.ssid));
    CHECK(!storage_wifi_config_is_valid(&config));
    CHECK(storage_wifi_config_classify(&config) == STORAGE_CONFIG_STATUS_CORRUPT);

    config = valid_config();
    memset(config.password, 'a', sizeof(config.password));
    CHECK(!storage_wifi_config_is_valid(&config));
}

static void test_rejects_out_of_range_channels(void)
{
    storage_wifi_config_t config = valid_config();

    config.channel = 0;
    CHECK(!storage_wifi_config_is_valid(&config));

    config.channel = 14;
    CHECK(!storage_wifi_config_is_valid(&config));

    CHECK(storage_wifi_channel_is_valid(1));
    CHECK(storage_wifi_channel_is_valid(13));
    CHECK(!storage_wifi_channel_is_valid(255));
}

static void test_safe_ranges_clamp_channel(void)
{
    storage_wifi_config_t config = valid_config();
    config.channel = 99;

    storage_wifi_config_apply_safe_ranges(&config);
    CHECK(config.channel == STORAGE_WIFI_CHANNEL_DEFAULT);

    config.channel = 11;
    storage_wifi_config_apply_safe_ranges(&config);
    CHECK(config.channel == 11);
}

static void test_classifies_missing_valid_and_corrupt_configs(void)
{
    storage_wifi_config_t config = {0};

    CHECK(storage_wifi_config_classify(&config) == STORAGE_CONFIG_STATUS_MISSING);

    config = valid_config();
    CHECK(storage_wifi_config_classify(&config) == STORAGE_CONFIG_STATUS_VALID);

    config = valid_config();
    config.password[0] = '\0';
    CHECK(storage_wifi_config_classify(&config) == STORAGE_CONFIG_STATUS_CORRUPT);

    config = valid_config();
    config.channel = 99;
    CHECK(storage_wifi_config_classify(&config) == STORAGE_CONFIG_STATUS_CORRUPT);
}

static void test_secret_persistence_requires_encrypted_nvs(void)
{
    CHECK(!storage_config_secret_persistence_allowed(false));
    CHECK(storage_config_secret_persistence_allowed(true));
}

static void test_secure_zero_clears_secret_buffers(void)
{
    char secret[32];
    memset(secret, 'x', sizeof(secret));

    storage_secure_zero(secret, sizeof(secret));

    for (size_t i = 0; i < sizeof(secret); ++i) {
        CHECK(secret[i] == '\0');
    }
}

int main(void)
{
    test_accepts_valid_wpa2_ap_config();
    test_rejects_empty_or_short_secrets();
    test_rejects_unterminated_strings();
    test_rejects_out_of_range_channels();
    test_safe_ranges_clamp_channel();
    test_classifies_missing_valid_and_corrupt_configs();
    test_secret_persistence_requires_encrypted_nvs();
    test_secure_zero_clears_secret_buffers();
    return 0;
}
