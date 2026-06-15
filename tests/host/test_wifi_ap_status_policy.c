#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifi_ap_status_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_driver_active_overrides_stale_cached_ap_off(void)
{
    wifi_ap_status_policy_t cached = {
        .ip_addr = "192.168.4.1",
        .connected_clients = 0,
        .started = false,
    };

    wifi_ap_status_policy_t status = wifi_ap_status_reconcile(cached, true, 0);

    CHECK(status.started);
    CHECK(status.connected_clients == 0);
    CHECK(strcmp(status.ip_addr, "192.168.4.1") == 0);
}

static void test_driver_client_count_is_authoritative_when_ap_is_active(void)
{
    wifi_ap_status_policy_t cached = {
        .ip_addr = "192.168.4.1",
        .connected_clients = 3,
        .started = true,
    };

    wifi_ap_status_policy_t status = wifi_ap_status_reconcile(cached, true, 1);

    CHECK(status.started);
    CHECK(status.connected_clients == 1);
}

static void test_cached_off_remains_off_when_driver_is_not_active(void)
{
    wifi_ap_status_policy_t cached = {
        .ip_addr = "192.168.4.1",
        .connected_clients = 2,
        .started = false,
    };

    wifi_ap_status_policy_t status = wifi_ap_status_reconcile(cached, false, 0);

    CHECK(!status.started);
    CHECK(status.connected_clients == 0);
}

int main(void)
{
    test_driver_active_overrides_stale_cached_ap_off();
    test_driver_client_count_is_authoritative_when_ap_is_active();
    test_cached_off_remains_off_when_driver_is_not_active();
    return 0;
}
