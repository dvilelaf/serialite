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
        .max_clients = 4,
    };
    strcpy(config.ssid, "ESP32-KVM-ABCDEF");
    strcpy(config.password, "12345678abcdef");
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
}

static void test_rejects_out_of_range_network_values(void)
{
    storage_wifi_config_t config = valid_config();

    config.channel = 0;
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    config.channel = 14;
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    config.max_clients = 0;
    CHECK(!storage_wifi_config_is_valid(&config));

    config = valid_config();
    config.max_clients = 5;
    CHECK(!storage_wifi_config_is_valid(&config));
}

static void test_safe_ranges_clamp_numeric_values(void)
{
    storage_wifi_config_t config = valid_config();
    config.channel = 99;
    config.max_clients = 99;

    storage_wifi_config_apply_safe_ranges(&config);

    CHECK(config.channel == 6);
    CHECK(config.max_clients == 4);
}

int main(void)
{
    test_accepts_valid_wpa2_ap_config();
    test_rejects_empty_or_short_secrets();
    test_rejects_out_of_range_network_values();
    test_safe_ranges_clamp_numeric_values();
    return 0;
}
