#include "demo_serial_runtime.h"

#include "app_watchdog.h"
#include "demo_serial.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "terminal_bridge.h"
#include "usb_console.h"

static const char *TAG = "demo_serial";

#define DEMO_SERIAL_TASK_STACK 3072
#define DEMO_SERIAL_TASK_PRIORITY 2
#define DEMO_SERIAL_BUF_SIZE 192

static demo_serial_state_t s_state;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_storage;
static TaskHandle_t s_task;
static bool s_started;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void demo_serial_task(void *arg)
{
    (void)arg;
    char buf[DEMO_SERIAL_BUF_SIZE];

    while (true) {
        if (usb_console_get_status().connected) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const bool was_active = demo_serial_is_active(&s_state);
            demo_serial_stop(&s_state);
            xSemaphoreGive(s_lock);
            if (was_active) {
                event_log_append(EVENT_LOG_INFO, now_ms(), "demo serial stopped: real USB connected");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t len = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        len = demo_serial_next_output(&s_state, now_ms(), buf, sizeof(buf));
        xSemaphoreGive(s_lock);
        if (len > 0 && usb_console_get_status().connected) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            demo_serial_stop(&s_state);
            xSemaphoreGive(s_lock);
            event_log_append(EVENT_LOG_INFO, now_ms(), "demo serial stopped: real USB connected");
            len = 0;
        }
        if (len > 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (usb_console_get_status().connected || !demo_serial_is_active(&s_state)) {
                len = 0;
            }
            if (len > 0) {
                terminal_bridge_publish_usb_output((const uint8_t *)buf, len);
            }
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t demo_serial_runtime_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(terminal_bridge_start(), TAG, "bridge start failed");
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock create failed");
    demo_serial_init(&s_state);

    const BaseType_t ok = xTaskCreate(
        demo_serial_task,
        "demo_serial",
        DEMO_SERIAL_TASK_STACK,
        NULL,
        DEMO_SERIAL_TASK_PRIORITY,
        &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    s_started = true;
    ESP_LOGI(TAG, "demo serial runtime ready");
    return ESP_OK;
}

esp_err_t demo_serial_runtime_enable(bool writer_active)
{
    ESP_RETURN_ON_ERROR(demo_serial_runtime_start(), TAG, "runtime start failed");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = demo_serial_start(&s_state, usb_console_get_status().connected, writer_active, now_ms());
    xSemaphoreGive(s_lock);
    if (!ok) {
        event_log_append(EVENT_LOG_WARN, now_ms(), "demo serial start rejected");
        return ESP_ERR_INVALID_STATE;
    }

    event_log_append(EVENT_LOG_INFO, now_ms(), "demo serial started");
    return ESP_OK;
}

void demo_serial_runtime_disable(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    demo_serial_stop(&s_state);
    xSemaphoreGive(s_lock);
    event_log_append(EVENT_LOG_INFO, now_ms(), "demo serial stopped");
}

demo_serial_runtime_status_t demo_serial_runtime_get_status(void)
{
    demo_serial_runtime_status_t status = {0};
    if (s_lock == NULL) {
        return status;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    status.active = demo_serial_is_active(&s_state);
    status.bytes_emitted = demo_serial_bytes_emitted(&s_state);
    xSemaphoreGive(s_lock);
    return status;
}
