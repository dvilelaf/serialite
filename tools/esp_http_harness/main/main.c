#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_server.h"

#include <signal.h>

static const char *TAG = "http_harness";

void app_main(void)
{
    (void)signal(SIGPIPE, SIG_IGN);

    const esp_err_t event_err = esp_event_loop_create_default();
    ESP_LOGI(TAG, "esp_event_loop_create_default -> %s", esp_err_to_name(event_err));

    const web_server_config_t config = {
        .web_password = "alpha zoom",
        .web_password_hash_configured = false,
        .tls_identity = NULL,
        .tls_fingerprint_displayed_locally = false,
        .rotate_credentials = NULL,
        .rotate_credentials_ctx = NULL,
        .export_config = NULL,
        .import_config = NULL,
        .config_ctx = NULL,
    };

    const esp_err_t err = web_server_start(&config);
    ESP_LOGI(TAG, "web_server_start -> %s", esp_err_to_name(err));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
