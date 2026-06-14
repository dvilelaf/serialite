#include "app_watchdog.h"

#include "event_log.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "app_watchdog";

esp_err_t app_watchdog_register_current_task(const char *task_name)
{
    const esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "task watchdog registration failed for %s: %s", task_name, esp_err_to_name(err));
        event_log_append(EVENT_LOG_WARN, 0, "task watchdog registration failed");
        return err;
    }

    ESP_LOGI(TAG, "task watchdog registered: %s", task_name);
    return ESP_OK;
}

void app_watchdog_reset_current_task(void)
{
    const esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "task watchdog reset failed: %s", esp_err_to_name(err));
    }
}
