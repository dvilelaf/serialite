#include "network_identity.h"

#include "network_identity_policy.h"

#include "esp_check.h"
#include "esp_log.h"
#include "mdns.h"

#include <stdbool.h>

static const char *TAG = "network_identity";
static bool s_started;

static esp_err_t validate_config(const network_identity_config_t *config)
{
    const network_identity_policy_config_t policy_config = {
        .hostname = config == NULL ? NULL : config->hostname,
        .instance_name = config == NULL ? NULL : config->instance_name,
        .service_type = config == NULL ? NULL : config->service_type,
        .port = config == NULL ? 0 : config->port,
        .ttl_seconds = config == NULL ? 0 : config->ttl_seconds,
    };
    return network_identity_config_is_valid(&policy_config) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t network_identity_start(const network_identity_config_t *config)
{
    ESP_RETURN_ON_ERROR(validate_config(config), TAG, "invalid mDNS config");
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mDNS init failed");

    esp_err_t err = mdns_hostname_set(config->hostname);
    if (err == ESP_OK) {
        err = mdns_instance_name_set(config->instance_name);
    }
    if (err == ESP_OK) {
        err = mdns_service_add(config->instance_name, config->service_type, "_tcp", config->port, NULL, 0);
    }
    if (err != ESP_OK) {
        mdns_free();
        ESP_LOGE(TAG, "mDNS identity failed: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "mDNS ready: %s://%s.local", config->port == 443 ? "https" : "http", config->hostname);
    return ESP_OK;
}

void network_identity_stop(void)
{
    if (!s_started) {
        return;
    }
    mdns_free();
    s_started = false;
}
