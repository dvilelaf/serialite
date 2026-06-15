#include "reset_control.h"

#include "board_waveshare_amoled.h"
#include "event_log.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_button_gesture.h"
#include "power_control.h"
#include "reset_gesture.h"
#include "storage.h"
#include "web_server.h"

static const char *TAG = "reset_control";

#define RESET_CONTROL_TASK_STACK 2048
#define RESET_CONTROL_TASK_PRIORITY 2
#define RESET_CONTROL_POLL_TICKS pdMS_TO_TICKS(100)

static bool s_started;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void reset_control_task(void *arg)
{
    (void)arg;
    power_button_gesture_t power_gesture;
    reset_gesture_t reset_gesture;
    power_button_gesture_init(&power_gesture);
    reset_gesture_init(&reset_gesture);

    while (true) {
        const bool lock_button_active = board_waveshare_amoled_security_button_active();
        const bool wake_button_active = board_waveshare_amoled_wake_button_active();
        const uint64_t timestamp_ms = now_ms();

        const power_button_gesture_event_t power_event =
            power_button_gesture_update(&power_gesture, lock_button_active, timestamp_ms);
        if (power_event == POWER_BUTTON_GESTURE_SHORT_RELEASE) {
            ESP_LOGW(TAG, "emergency lock toggle gesture accepted");
            event_log_append(EVENT_LOG_SECURITY, timestamp_ms, "emergency lock toggle gesture accepted");
            const esp_err_t err = web_server_emergency_lock_toggle();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "emergency lock toggle failed: %s", esp_err_to_name(err));
                event_log_append(EVENT_LOG_ERROR, timestamp_ms, "emergency lock toggle failed");
            }
        } else if (power_event == POWER_BUTTON_GESTURE_LONG_HOLD) {
            ESP_LOGW(TAG, "power-off gesture accepted");
            event_log_append(EVENT_LOG_SECURITY, timestamp_ms, "power-off gesture accepted");
            const esp_err_t err = power_control_power_off();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "power-off failed: %s", esp_err_to_name(err));
                event_log_append(EVENT_LOG_ERROR, timestamp_ms, "power-off failed");
            }
        }

        if (reset_gesture_update(&reset_gesture, wake_button_active, timestamp_ms)) {
            ESP_LOGW(TAG, "factory reset gesture accepted; erasing stored configuration");
            event_log_append(EVENT_LOG_SECURITY, timestamp_ms, "factory reset gesture accepted");
            const esp_err_t err = storage_factory_reset();
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
            ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(err));
            event_log_append(EVENT_LOG_ERROR, timestamp_ms, "factory reset failed");
        }

        vTaskDelay(RESET_CONTROL_POLL_TICKS);
    }
}

esp_err_t reset_control_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    const BaseType_t ok = xTaskCreate(
        reset_control_task,
        "reset_control",
        RESET_CONTROL_TASK_STACK,
        NULL,
        RESET_CONTROL_TASK_PRIORITY,
        NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "reset task create failed");

    s_started = true;
    return ESP_OK;
}
