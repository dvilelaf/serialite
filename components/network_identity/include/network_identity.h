#pragma once

#include "esp_err.h"
#include <stdint.h>

#define NETWORK_IDENTITY_HOSTNAME "kvm"
#define NETWORK_IDENTITY_SERVICE_NAME "ESP32-KVM"
#define NETWORK_IDENTITY_HTTP_SERVICE "_http"
#define NETWORK_IDENTITY_LOCAL_URL "http://kvm.local"

typedef struct {
    const char *hostname;
    const char *instance_name;
    const char *service_type;
    uint16_t port;
    uint32_t ttl_seconds;
} network_identity_config_t;

esp_err_t network_identity_start(const network_identity_config_t *config);
void network_identity_stop(void);
