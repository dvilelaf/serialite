#include "network_identity_policy.h"

#include <assert.h>
#include <string.h>

static network_identity_policy_config_t valid_config(void)
{
    return (network_identity_policy_config_t){
        .hostname = "kvm",
        .instance_name = "ESP32-KVM",
        .service_type = "_http",
        .port = 80,
        .ttl_seconds = 120,
    };
}

static void accepts_valid_mdns_identity(void)
{
    network_identity_policy_config_t config = valid_config();

    assert(network_identity_config_is_valid(&config));
}

static void rejects_invalid_hostnames(void)
{
    network_identity_policy_config_t config = valid_config();

    config.hostname = "";
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.hostname = "-kvm";
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.hostname = "kvm-";
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.hostname = "kvm.local";
    assert(!network_identity_config_is_valid(&config));
}

static void rejects_invalid_services(void)
{
    network_identity_policy_config_t config = valid_config();

    config.service_type = "http";
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.service_type = "_HTTP";
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.service_type = "_";
    assert(!network_identity_config_is_valid(&config));
}

static void rejects_unsafe_ports_and_ttls(void)
{
    network_identity_policy_config_t config = valid_config();

    config.port = 0;
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.ttl_seconds = 30;
    assert(!network_identity_config_is_valid(&config));

    config = valid_config();
    config.ttl_seconds = 90000;
    assert(!network_identity_config_is_valid(&config));
}

int main(void)
{
    accepts_valid_mdns_identity();
    rejects_invalid_hostnames();
    rejects_invalid_services();
    rejects_unsafe_ports_and_ttls();
    return 0;
}
