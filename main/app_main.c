#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "lvgl_ui.h"
#include "network_identity.h"
#include "reset_control.h"
#include "storage.h"
#include "startup_policy.h"
#include "terminal_bridge.h"
#include "usb_console.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "esp32_kvm";

static const char *const PASSWORD_WORDS[] = {
    "amber", "anchor", "apple", "artist", "atlas", "autumn", "badge", "baker",
    "bamboo", "beacon", "beaver", "berry", "bison", "blossom", "border", "bottle",
    "bridge", "bronze", "bucket", "butter", "cactus", "camera", "candle", "canyon",
    "castle", "cedar", "celery", "cement", "cherry", "circle", "clover", "cobalt",
    "coffee", "comet", "copper", "cotton", "cradle", "cricket", "crystal", "daisy",
    "dakota", "delta", "desert", "diamond", "dolphin", "dragon", "drift", "eagle",
    "earth", "echo", "ember", "engine", "falcon", "farm", "feather", "ferry",
    "field", "forest", "fossil", "garden", "ginger", "glacier", "golden", "grape",
    "harbor", "hazel", "helmet", "hollow", "honest", "horizon", "island", "ivory",
    "jacket", "jaguar", "jasmine", "jigsaw", "juniper", "kernel", "kettle", "kiwi",
    "ladder", "lagoon", "lantern", "laser", "lemon", "linen", "lizard", "magnet",
    "maple", "marble", "market", "meadow", "melon", "meteor", "mirror", "misty",
    "monkey", "museum", "nectar", "needle", "nickel", "oasis", "olive", "orange",
    "orbit", "orchid", "otter", "oxygen", "paddle", "paper", "parrot", "pebble",
    "pepper", "pickle", "planet", "pocket", "prairie", "quartz", "rabbit", "radar",
    "raven", "record", "river", "rocket", "saddle", "saffron", "sailor", "salmon",
    "saturn", "shadow", "silver", "signal", "sketch", "socket", "sparrow", "spider",
    "spring", "square", "stable", "station", "stone", "summer", "sunset", "tackle",
    "tango", "temple", "ticket", "timber", "tomato", "tunnel", "turkey", "turtle",
    "velvet", "violet", "voyage", "walnut", "wander", "window", "winter", "wizard",
    "yellow", "yonder", "zephyr", "zigzag", "acorn", "banana", "basket", "button",
    "carbon", "carrot", "cobalt", "coral", "denim", "donkey", "fabric", "finger",
    "flower", "galaxy", "garage", "garlic", "goblin", "granite", "hammer", "hazard",
    "icicle", "insect", "jungle", "kitten", "koala", "legend", "little", "lobster",
    "lunar", "memory", "mineral", "muffin", "napkin", "native", "noodle", "number",
    "onion", "opal", "palace", "pencil", "pepper", "person", "pigeon", "pirate",
    "plasma", "potato", "puzzle", "quiver", "ribbon", "robot", "salsa", "school",
    "season", "shrimp", "simple", "singer", "smoke", "snow", "spirit", "sponge",
    "staple", "studio", "sugar", "switch", "tablet", "thunder", "tiger", "toast",
    "topaz", "travel", "triple", "trumpet", "velcro", "vendor", "vessel", "walrus",
    "water", "whisper", "willow", "winner", "yogurt", "zebra", "zenith", "zipper",
    "almond", "arcade", "arctic", "balance", "beetle", "breeze", "broker", "canvas",
    "casino", "citron", "cookie", "cosmic", "cotton", "damage", "dinner", "doodle",
    "effect", "fabric", "famous", "folder", "gentle", "glider", "guitar", "honor",
    "ignite", "jumper", "kingdom", "letter", "marine", "motion", "nectar", "pepper",
    "public", "random", "remote", "rescue", "sample", "secure", "server", "socket",
    "system", "tandem", "urgent", "vector", "vision", "volume", "writer", "zodiac",
};

enum {
    PASSWORD_WORD_COUNT = sizeof(PASSWORD_WORDS) / sizeof(PASSWORD_WORDS[0]),
};

static void log_init_result(const char *name, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s ready", name);
        return;
    }

    ESP_LOGE(TAG, "%s failed: %s", name, esp_err_to_name(err));
}

static esp_err_t init_nvs(bool *recovered)
{
    if (recovered == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *recovered = false;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        *recovered = true;
        err = nvs_flash_init();
    }

    return err;
}

static esp_err_t generate_human_password(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t word_indexes[4] = {0};
    esp_fill_random(word_indexes, sizeof(word_indexes));
    const int password_len = snprintf(
        out,
        out_size,
        "%s-%s-%s-%s",
        PASSWORD_WORDS[word_indexes[0] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[1] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[2] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[3] % PASSWORD_WORD_COUNT]);
    if (password_len < 0 || password_len >= (int)out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t generate_ephemeral_wifi_config(kvm_wifi_ap_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(config->ssid, "KVM", sizeof(config->ssid));

    ESP_RETURN_ON_ERROR(generate_human_password(config->password, sizeof(config->password)), TAG, "AP password generation failed");
    config->channel = 6;
    config->max_clients = 4;

    ESP_LOGW(TAG, "generated ephemeral AP credentials for ssid=%s", config->ssid);
    ESP_LOGW(TAG, "AP password is not logged; local display flow will expose it during provisioning");
    return ESP_OK;
}

void app_main(void)
{
    bool nvs_recovered = false;
    ESP_ERROR_CHECK(init_nvs(&nvs_recovered));

    storage_config_t config;
    const esp_err_t storage_err = storage_init();
    log_init_result("storage", storage_err);
    ESP_ERROR_CHECK(storage_err);
    log_init_result("terminal_bridge", terminal_bridge_start());
    const esp_err_t config_err = storage_load_config(&config);
    if (config_err != ESP_OK && config_err != ESP_ERR_STORAGE_CONFIG_CORRUPT) {
        ESP_ERROR_CHECK(config_err);
    }

    kvm_wifi_ap_config_t mapped_wifi_config = {
        .channel = config.wifi.channel,
        .max_clients = config.wifi.max_clients,
    };
    strlcpy(mapped_wifi_config.ssid, config.wifi.ssid, sizeof(mapped_wifi_config.ssid));
    strlcpy(mapped_wifi_config.password, config.wifi.password, sizeof(mapped_wifi_config.password));

    bool ephemeral_credentials = false;
    const storage_config_status_t config_status = storage_wifi_config_classify(&config.wifi);
    if (nvs_recovered || config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT || config_status != STORAGE_CONFIG_STATUS_VALID) {
        if (config_err == ESP_ERR_STORAGE_CONFIG_CORRUPT) {
            ESP_LOGE(TAG, "stored AP config is corrupt; entering physical setup flow with regenerated credentials");
        }
        ESP_ERROR_CHECK(generate_ephemeral_wifi_config(&mapped_wifi_config));
        ephemeral_credentials = true;
    }
    storage_secure_zero(config.wifi.password, sizeof(config.wifi.password));

    char web_password[WIFI_AP_PASSWORD_MAX_LEN];
    ESP_ERROR_CHECK(generate_human_password(web_password, sizeof(web_password)));

    const lvgl_ui_boot_status_t ui_status = {
        .ssid = mapped_wifi_config.ssid,
        .password = mapped_wifi_config.password,
        .web_password = web_password,
        .ip_addr = "192.168.4.1",
        .usb_connected = false,
    };
    const esp_err_t ui_err = lvgl_ui_start(&ui_status);
    log_init_result("lvgl_ui", ui_err);
    if (ui_err == ESP_OK) {
        log_init_result("reset_control", reset_control_start());
    }
    if (startup_policy_after_ui(ui_err == ESP_OK, ephemeral_credentials) == STARTUP_POLICY_SKIP_AP) {
        ESP_LOGE(TAG, "AP skipped: ephemeral password cannot be safely exposed without local display");
        storage_secure_zero(mapped_wifi_config.password, sizeof(mapped_wifi_config.password));
        storage_secure_zero(web_password, sizeof(web_password));
        log_init_result("usb_console", usb_console_start());
        return;
    }

    esp_err_t wifi_err = wifi_ap_start(&mapped_wifi_config);
    log_init_result("wifi_ap", wifi_err);
    storage_secure_zero(mapped_wifi_config.password, sizeof(mapped_wifi_config.password));
    log_init_result("usb_console", usb_console_start());
    if (wifi_err == ESP_OK) {
        const network_identity_config_t network_identity_config = {
            .hostname = NETWORK_IDENTITY_HOSTNAME,
            .instance_name = NETWORK_IDENTITY_SERVICE_NAME,
            .service_type = NETWORK_IDENTITY_HTTP_SERVICE,
            .port = 80,
            .ttl_seconds = 120,
        };
        const esp_err_t identity_err = network_identity_start(&network_identity_config);
        if (identity_err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS unavailable; use http://192.168.4.1: %s", esp_err_to_name(identity_err));
        }
        const web_server_config_t web_config = {
            .web_password = web_password,
        };
        const esp_err_t web_err = web_server_start(&web_config);
        storage_secure_zero(web_password, sizeof(web_password));
        log_init_result("web_server", web_err);
        if (startup_policy_after_web(true, web_err == ESP_OK) == STARTUP_POLICY_STOP_AP) {
            ESP_LOGE(TAG, "AP stopped: web/auth service failed after WiFi startup");
            log_init_result("wifi_ap_stop", wifi_ap_stop());
        }
    } else {
        storage_secure_zero(web_password, sizeof(web_password));
        ESP_LOGE(TAG, "web_server skipped because AP did not start");
    }
}
