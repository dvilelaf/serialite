#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETWORK_IDENTITY_HOSTNAME_MAX_LEN 63
#define NETWORK_IDENTITY_SERVICE_LABEL_MAX_LEN 63

typedef struct {
    const char *hostname;
    const char *instance_name;
    const char *service_type;
    uint16_t port;
    uint32_t ttl_seconds;
} network_identity_policy_config_t;

bool network_identity_config_is_valid(const network_identity_policy_config_t *config);
