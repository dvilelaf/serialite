/*
 * Runs the real web_server on Linux (ESP-IDF linux target) against stub
 * hardware. Callbacks print HARNESS markers that run_http_harness_tests.sh
 * uses to prove rejected requests never reached them.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_server.h"

static const char *TAG = "http_harness";
static const char *const ALLOWED_HOSTS[] = {"127.0.0.1", "localhost"};

static void mark(const char *what)
{
    printf("HARNESS %s\n", what);
    fflush(stdout);
}

static esp_err_t rotate_credentials(void *ctx)
{
    (void)ctx;
    mark("rotate_credentials");
    /* Pretend there is no display, so the server refuses instead of rebooting the harness. */
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t export_config(char *out, size_t out_size, void *ctx)
{
    (void)ctx;
    mark("export_config");
    const int n = snprintf(out, out_size, "{\"schema\":\"harness\"}");
    return n > 0 && (size_t)n < out_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t import_config(const char *json, void *ctx)
{
    (void)ctx;
    mark("import_config");
    return strcmp(json, "{\"schema\":\"harness\"}") == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void app_main(void)
{
    (void)signal(SIGPIPE, SIG_IGN);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const web_server_config_t config = {
        .allowed_hosts = ALLOWED_HOSTS,
        .allowed_host_count = sizeof(ALLOWED_HOSTS) / sizeof(ALLOWED_HOSTS[0]),
        .port = 8080,
        .rotate_credentials = rotate_credentials,
        .export_config = export_config,
        .import_config = import_config,
    };
    const esp_err_t err = web_server_start(&config);
    ESP_LOGI(TAG, "web_server_start -> %s", esp_err_to_name(err));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
