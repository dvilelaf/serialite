#include "ap_guard.h"

#include "ap_exposure_policy.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "ap_guard";

#define AP_GUARD_TASK_STACK 3072
#define AP_GUARD_TASK_PRIORITY 2
#define AP_GUARD_INTERVAL_MS 10000

static volatile bool s_stopped_ap;

static void ap_guard_task(void *arg)
{
    (void)arg;
    ap_exposure_policy_state_t policy;
    ap_exposure_policy_init(&policy);

    /* Sleeps longer than the task watchdog period, so it is not registered with it. */
    while (!s_stopped_ap) {
        vTaskDelay(pdMS_TO_TICKS(AP_GUARD_INTERVAL_MS));
        const uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
        const wifi_ap_status_t wifi = wifi_ap_get_status();
        const web_server_status_t web = web_server_get_status();

        if (wifi.connected_clients == 0 && web.ws_client_count > 0) {
            (void)web_server_close_clients();
        }
        if (ap_exposure_policy_evaluate(&policy, wifi.started, wifi.connected_clients, web.ws_client_count, now_ms) ==
            AP_EXPOSURE_STOP_AP) {
            event_log_append(EVENT_LOG_SECURITY, now_ms, "AP idle timeout; stopping AP");
            const esp_err_t err = wifi_ap_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "AP stop failed: %s", esp_err_to_name(err));
            }
            s_stopped_ap = true;
        }
    }
    ESP_LOGW(TAG, "AP stopped after inactivity; press the primary button to restart it");
    vTaskDelete(NULL);
}

esp_err_t ap_guard_start(void)
{
    const BaseType_t ok = xTaskCreate(ap_guard_task, "ap_guard", AP_GUARD_TASK_STACK, NULL, AP_GUARD_TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

bool ap_guard_stopped_ap(void)
{
    return s_stopped_ap;
}
