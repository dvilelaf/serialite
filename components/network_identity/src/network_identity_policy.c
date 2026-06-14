#include "network_identity_policy.h"

#include <string.h>

static bool is_hostname_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

static bool is_valid_hostname_label(const char *label)
{
    if (label == NULL) {
        return false;
    }

    const size_t len = strnlen(label, NETWORK_IDENTITY_HOSTNAME_MAX_LEN + 1);
    if (len == 0 || len > NETWORK_IDENTITY_HOSTNAME_MAX_LEN) {
        return false;
    }
    if (label[0] == '-' || label[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!is_hostname_char(label[i])) {
            return false;
        }
    }
    return true;
}

static bool is_valid_service_type(const char *service_type)
{
    if (service_type == NULL) {
        return false;
    }

    const size_t len = strnlen(service_type, NETWORK_IDENTITY_SERVICE_LABEL_MAX_LEN + 1);
    if (len < 2 || len > NETWORK_IDENTITY_SERVICE_LABEL_MAX_LEN || service_type[0] != '_') {
        return false;
    }
    for (size_t i = 1; i < len; i++) {
        const char c = service_type[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool is_valid_instance_name(const char *instance_name)
{
    if (instance_name == NULL) {
        return false;
    }

    const size_t len = strnlen(instance_name, NETWORK_IDENTITY_SERVICE_LABEL_MAX_LEN + 1);
    return len > 0 && len <= NETWORK_IDENTITY_SERVICE_LABEL_MAX_LEN;
}

bool network_identity_config_is_valid(const network_identity_policy_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (!is_valid_hostname_label(config->hostname)) {
        return false;
    }
    if (!is_valid_instance_name(config->instance_name)) {
        return false;
    }
    if (!is_valid_service_type(config->service_type)) {
        return false;
    }
    if (config->port == 0) {
        return false;
    }
    if (config->ttl_seconds < 60 || config->ttl_seconds > 86400) {
        return false;
    }
    return true;
}
