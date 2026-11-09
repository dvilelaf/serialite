/* Linux stand-in for the hardware component, built against its real header. */

#include <string.h>

#include "wifi_ap.h"

esp_err_t wifi_ap_start(const wifi_ap_settings_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t wifi_ap_stop(void)
{
    return ESP_OK;
}

wifi_ap_status_t wifi_ap_get_status(void)
{
    wifi_ap_status_t status = {.connected_clients = 1, .started = true};
    strcpy(status.ip_addr, "127.0.0.1");
    return status;
}
