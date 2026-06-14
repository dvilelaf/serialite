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

static void test_plaintext_legacy_secrets_are_scrubbed_without_encrypted_nvs(void)
{
    CHECK(storage_config_should_scrub_legacy_plaintext_secrets(false));
    CHECK(!storage_config_should_scrub_legacy_plaintext_secrets(true));
}

typedef struct {
    unsigned ssid_erases;
    unsigned password_erases;
    unsigned commits;
    storage_secret_erase_result_t erase_result;
    bool commit_result;
} scrub_test_ctx_t;

static storage_secret_erase_result_t scrub_test_erase(storage_secret_key_t key, void *ctx)
{
    scrub_test_ctx_t *test = (scrub_test_ctx_t *)ctx;
    if (key == STORAGE_SECRET_KEY_SSID) {
        test->ssid_erases++;
    } else if (key == STORAGE_SECRET_KEY_PASSWORD) {
        test->password_erases++;
    }
    return test->erase_result;
}

static bool scrub_test_commit(void *ctx)
{
    scrub_test_ctx_t *test = (scrub_test_ctx_t *)ctx;
    test->commits++;
    return test->commit_result;
}

static void test_scrub_erases_legacy_plaintext_secret_keys_and_commits(void)
{
    scrub_test_ctx_t test = {
        .erase_result = STORAGE_SECRET_ERASED,
        .commit_result = true,
    };

    CHECK(storage_config_scrub_legacy_plaintext_secrets(false, scrub_test_erase, scrub_test_commit, &test));
    CHECK(test.ssid_erases == 1);
    CHECK(test.password_erases == 1);
    CHECK(test.commits == 1);
}

static void test_scrub_skips_commit_when_legacy_secret_keys_are_missing(void)
{
    scrub_test_ctx_t test = {
        .erase_result = STORAGE_SECRET_NOT_FOUND,
        .commit_result = true,
    };

    CHECK(storage_config_scrub_legacy_plaintext_secrets(false, scrub_test_erase, scrub_test_commit, &test));
    CHECK(test.ssid_erases == 1);
    CHECK(test.password_erases == 1);
    CHECK(test.commits == 0);
}

static void test_scrub_fails_closed_on_erase_or_commit_error(void)
{
    scrub_test_ctx_t erase_error = {
        .erase_result = STORAGE_SECRET_ERASE_ERROR,
        .commit_result = true,
    };
    CHECK(!storage_config_scrub_legacy_plaintext_secrets(false, scrub_test_erase, scrub_test_commit, &erase_error));

    scrub_test_ctx_t commit_error = {
        .erase_result = STORAGE_SECRET_ERASED,
        .commit_result = false,
    };
    CHECK(!storage_config_scrub_legacy_plaintext_secrets(false, scrub_test_erase, scrub_test_commit, &commit_error));
    CHECK(commit_error.commits == 1);
}

static void test_scrub_does_not_touch_keys_when_nvs_is_encrypted(void)
{
    scrub_test_ctx_t test = {
        .erase_result = STORAGE_SECRET_ERASED,
        .commit_result = true,
    };

    CHECK(storage_config_scrub_legacy_plaintext_secrets(true, scrub_test_erase, scrub_test_commit, &test));
    CHECK(test.ssid_erases == 0);
    CHECK(test.password_erases == 0);
    CHECK(test.commits == 0);
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
    test_rejects_out_of_range_network_values();
    test_safe_ranges_clamp_numeric_values();
    test_classifies_missing_valid_and_corrupt_configs();
    test_secret_persistence_requires_encrypted_nvs();
    test_plaintext_legacy_secrets_are_scrubbed_without_encrypted_nvs();
    test_scrub_erases_legacy_plaintext_secret_keys_and_commits();
    test_scrub_skips_commit_when_legacy_secret_keys_are_missing();
    test_scrub_fails_closed_on_erase_or_commit_error();
    test_scrub_does_not_touch_keys_when_nvs_is_encrypted();
    test_secure_zero_clears_secret_buffers();
    return 0;
}
