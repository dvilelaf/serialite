#include "wifi_ap.h"
#include <string.h>
esp_err_t wifi_ap_start(const kvm_wifi_ap_config_t *config) { (void)config; return ESP_OK; }
esp_err_t wifi_ap_stop(void) { return ESP_OK; }
wifi_ap_status_t wifi_ap_get_status(void) { wifi_ap_status_t s = {.connected_clients = 1, .started = true}; strcpy(s.ip_addr, "127.0.0.1"); return s; }
