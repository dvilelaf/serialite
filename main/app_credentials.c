#include "app_credentials.h"

#include <string.h>

#include "config_transfer.h"
#include "credentials.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "storage.h"

static const char *TAG = "app_credentials";

#define RTC_CACHE_MAGIC 0x53524c54U /* "SRLT" */
#define RTC_CACHE_VERSION 2U
#define PASSWORD_ATTEMPTS 3

/* Survives software restarts so a reboot does not change the password under the operator. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc;
    char ssid[STORAGE_SSID_MAX_LEN];
    char phrase[STORAGE_PASSWORD_MAX_LEN];
    uint8_t channel;
} rtc_cache_t;

static RTC_NOINIT_ATTR rtc_cache_t s_rtc_cache;

static bool nvs_encryption_enabled(void)
{
#if CONFIG_NVS_ENCRYPTION
    return true;
#else
    return false;
#endif
}

static uint32_t rtc_cache_crc(const rtc_cache_t *cache)
{
    return esp_rom_crc32_le(0, (const uint8_t *)cache->ssid, sizeof(*cache) - offsetof(rtc_cache_t, ssid));
}

static bool phrase_valid(const char *phrase)
{
    return credentials_human_phrase_matches_policy(phrase, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT);
}

static bool rtc_cache_valid(void)
{
    return s_rtc_cache.magic == RTC_CACHE_MAGIC &&
           s_rtc_cache.version == RTC_CACHE_VERSION &&
           s_rtc_cache.crc == rtc_cache_crc(&s_rtc_cache) &&
           strnlen(s_rtc_cache.ssid, sizeof(s_rtc_cache.ssid)) < sizeof(s_rtc_cache.ssid) &&
           strnlen(s_rtc_cache.phrase, sizeof(s_rtc_cache.phrase)) < sizeof(s_rtc_cache.phrase) &&
           storage_wifi_channel_is_valid(s_rtc_cache.channel) &&
           phrase_valid(s_rtc_cache.phrase);
}

static void rtc_cache_store(const app_credentials_t *creds)
{
    memset(&s_rtc_cache, 0, sizeof(s_rtc_cache));
    strlcpy(s_rtc_cache.ssid, creds->ssid, sizeof(s_rtc_cache.ssid));
    strlcpy(s_rtc_cache.phrase, creds->phrase, sizeof(s_rtc_cache.phrase));
    s_rtc_cache.channel = creds->channel;
    s_rtc_cache.magic = RTC_CACHE_MAGIC;
    s_rtc_cache.version = RTC_CACHE_VERSION;
    s_rtc_cache.crc = rtc_cache_crc(&s_rtc_cache);
}

void app_credentials_forget_cached(void)
{
    storage_secure_zero(&s_rtc_cache, sizeof(s_rtc_cache));
}

void app_credentials_wipe(app_credentials_t *creds)
{
    storage_secure_zero(creds, sizeof(*creds));
}

static bool warm_reset(void)
{
    switch (esp_reset_reason()) {
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return true;
        default:
            return false;
    }
}

static bool hardware_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return true;
}

static esp_err_t generate_phrase(char *out, size_t out_size)
{
    for (int attempt = 0; attempt < PASSWORD_ATTEMPTS; ++attempt) {
        if (credentials_generate_human_password(out, out_size, hardware_random, NULL) == CREDENTIALS_OK) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static void to_storage(const app_credentials_t *creds, storage_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->wifi.ssid, creds->ssid, sizeof(out->wifi.ssid));
    strlcpy(out->wifi.password, creds->phrase, sizeof(out->wifi.password));
    out->wifi.channel = creds->channel;
}

esp_err_t app_credentials_load(app_credentials_t *out, bool storage_available)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "missing output");
    memset(out, 0, sizeof(*out));

    storage_config_t stored = {0};
    bool stored_valid = false;
    if (storage_available) {
        const esp_err_t err = storage_load_config(&stored);
        if (err == ESP_ERR_STORAGE_CONFIG_CORRUPT) {
            ESP_LOGE(TAG, "stored AP config is corrupt; generating new credentials");
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "stored AP config unreadable: %s", esp_err_to_name(err));
        }
        stored_valid = err == ESP_OK &&
                       storage_wifi_config_classify(&stored.wifi) == STORAGE_CONFIG_STATUS_VALID &&
                       phrase_valid(stored.wifi.password);
    }

    const credential_boot_facts_t facts = {
        .stored_valid = stored_valid,
        .rtc_cache_valid = rtc_cache_valid(),
        .warm_reset = warm_reset(),
    };
    out->source = credential_boot_policy_select(&facts);
    switch (out->source) {
        case CREDENTIAL_SOURCE_STORED:
            strlcpy(out->ssid, stored.wifi.ssid, sizeof(out->ssid));
            strlcpy(out->phrase, stored.wifi.password, sizeof(out->phrase));
            out->channel = stored.wifi.channel;
            app_credentials_forget_cached();
            break;
        case CREDENTIAL_SOURCE_RTC_CACHE:
            strlcpy(out->ssid, s_rtc_cache.ssid, sizeof(out->ssid));
            strlcpy(out->phrase, s_rtc_cache.phrase, sizeof(out->phrase));
            out->channel = s_rtc_cache.channel;
            ESP_LOGW(TAG, "reusing the password from before the restart");
            break;
        case CREDENTIAL_SOURCE_GENERATE:
        default:
            strlcpy(out->ssid, CONFIG_SERIALITE_AP_SSID, sizeof(out->ssid));
            out->channel = CONFIG_SERIALITE_AP_CHANNEL;
            if (generate_phrase(out->phrase, sizeof(out->phrase)) != ESP_OK) {
                storage_secure_zero(&stored, sizeof(stored));
                app_credentials_wipe(out);
                return ESP_FAIL;
            }
            rtc_cache_store(out);
            ESP_LOGW(TAG, "generated a new AP password; it is only shown on the device");
            break;
    }
    storage_secure_zero(&stored, sizeof(stored));
    return ESP_OK;
}

esp_err_t app_credentials_ap_config(const app_credentials_t *creds, uint8_t max_clients, wifi_ap_settings_t *out)
{
    ESP_RETURN_ON_FALSE(creds != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG, "missing argument");
    memset(out, 0, sizeof(*out));
    strlcpy(out->ssid, creds->ssid, sizeof(out->ssid));
    out->channel = creds->channel;
    out->max_clients = max_clients;
    if (!credentials_compact_human_phrase(creds->phrase, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT, out->password, sizeof(out->password))) {
        storage_secure_zero(out, sizeof(*out));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t app_credentials_rotate(app_credentials_t *creds)
{
    ESP_RETURN_ON_FALSE(creds != NULL, ESP_ERR_INVALID_ARG, TAG, "missing credentials");
    app_credentials_t next = *creds;
    if (generate_phrase(next.phrase, sizeof(next.phrase)) != ESP_OK) {
        app_credentials_wipe(&next);
        return ESP_FAIL;
    }

    if (storage_config_secret_persistence_allowed(nvs_encryption_enabled())) {
        storage_config_t record;
        to_storage(&next, &record);
        const esp_err_t err = storage_save_config(&record);
        storage_secure_zero(&record, sizeof(record));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "new password not saved: %s; keeping the current one", esp_err_to_name(err));
            app_credentials_wipe(&next);
            return err;
        }
        next.source = CREDENTIAL_SOURCE_STORED;
        app_credentials_forget_cached();
    } else {
        next.source = CREDENTIAL_SOURCE_RTC_CACHE;
        rtc_cache_store(&next);
    }

    app_credentials_wipe(creds);
    *creds = next;
    app_credentials_wipe(&next);
    return ESP_OK;
}

esp_err_t app_credentials_export(const app_credentials_t *creds, char *out, size_t out_size)
{
    storage_config_t record;
    to_storage(creds, &record);
    const config_transfer_result_t result = config_transfer_export_json(&record, out, out_size);
    storage_secure_zero(&record, sizeof(record));
    return result == CONFIG_TRANSFER_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t app_credentials_import(app_credentials_t *creds, const char *json)
{
    if (!storage_config_secret_persistence_allowed(nvs_encryption_enabled())) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    storage_config_t record;
    to_storage(creds, &record);
    const config_transfer_result_t result = config_transfer_import_json(json, &record);
    if (result != CONFIG_TRANSFER_OK) {
        ESP_LOGW(TAG, "config import rejected: %s", config_transfer_result_name(result));
        storage_secure_zero(&record, sizeof(record));
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t err = storage_save_config(&record);
    if (err == ESP_OK) {
        strlcpy(creds->ssid, record.wifi.ssid, sizeof(creds->ssid));
        creds->channel = record.wifi.channel;
        creds->source = CREDENTIAL_SOURCE_STORED;
    }
    storage_secure_zero(&record, sizeof(record));
    return err;
}
