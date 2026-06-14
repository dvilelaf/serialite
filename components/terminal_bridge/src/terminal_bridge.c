#include "terminal_bridge.h"

#include "terminal_scrollback.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include <string.h>

static const char *TAG = "terminal_bridge";

#define BRIDGE_TO_USB_BUFFER_SIZE 4096
#define BRIDGE_SCROLLBACK_SIZE 8192
#define BRIDGE_MAX_SUBSCRIBERS 4

typedef struct {
    terminal_bridge_output_cb_t callback;
    void *ctx;
} output_subscriber_t;

static StaticStreamBuffer_t s_to_usb_stream_struct;
static uint8_t s_to_usb_storage[BRIDGE_TO_USB_BUFFER_SIZE];
static uint8_t s_scrollback_storage[BRIDGE_SCROLLBACK_SIZE];
static terminal_scrollback_t s_scrollback;
static StreamBufferHandle_t s_to_usb_stream;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_storage;
static output_subscriber_t s_subscribers[BRIDGE_MAX_SUBSCRIBERS];
static terminal_bridge_status_t s_status;
static bool s_started;

esp_err_t terminal_bridge_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex create failed");

    s_to_usb_stream = xStreamBufferCreateStatic(
        sizeof(s_to_usb_storage),
        1,
        s_to_usb_storage,
        &s_to_usb_stream_struct);
    ESP_RETURN_ON_FALSE(s_to_usb_stream != NULL, ESP_ERR_NO_MEM, TAG, "to-usb stream create failed");

    memset(&s_status, 0, sizeof(s_status));
    memset(s_subscribers, 0, sizeof(s_subscribers));
    ESP_RETURN_ON_FALSE(
        terminal_scrollback_init(&s_scrollback, s_scrollback_storage, sizeof(s_scrollback_storage)) == TERMINAL_SCROLLBACK_OK,
        ESP_ERR_NO_MEM,
        TAG,
        "scrollback init failed");
    s_status.scrollback_capacity = sizeof(s_scrollback_storage);
    s_started = true;
    ESP_LOGI(TAG, "terminal bridge started");
    return ESP_OK;
}

esp_err_t terminal_bridge_register_output_callback(terminal_bridge_output_cb_t callback, void *ctx)
{
    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "callback is required");
    ESP_RETURN_ON_ERROR(terminal_bridge_start(), TAG, "bridge start failed");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < BRIDGE_MAX_SUBSCRIBERS; ++i) {
        if (s_subscribers[i].callback == NULL) {
            s_subscribers[i].callback = callback;
            s_subscribers[i].ctx = ctx;
            s_status.subscriber_count++;
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);

    return ESP_ERR_NO_MEM;
}

size_t terminal_bridge_publish_usb_output(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || terminal_bridge_start() != ESP_OK) {
        return 0;
    }

    output_subscriber_t subscribers[BRIDGE_MAX_SUBSCRIBERS] = {0};
    uint32_t subscriber_count = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.bytes_from_usb += len;
    terminal_scrollback_write(&s_scrollback, data, len);
    s_status.scrollback_retained = (uint32_t)terminal_scrollback_available(&s_scrollback);
    s_status.scrollback_dropped_oldest = terminal_scrollback_dropped_oldest(&s_scrollback);
    for (size_t i = 0; i < BRIDGE_MAX_SUBSCRIBERS; ++i) {
        if (s_subscribers[i].callback != NULL) {
            subscribers[subscriber_count++] = s_subscribers[i];
        }
    }
    xSemaphoreGive(s_lock);

    for (uint32_t i = 0; i < subscriber_count; ++i) {
        subscribers[i].callback(data, len, subscribers[i].ctx);
    }

    return len;
}

size_t terminal_bridge_submit_input(terminal_bridge_source_t source, const uint8_t *data, size_t len)
{
    (void)source;
    if (data == NULL || len == 0 || terminal_bridge_start() != ESP_OK) {
        return 0;
    }

    const size_t written = xStreamBufferSend(s_to_usb_stream, data, len, 0);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.bytes_to_usb += written;
    s_status.dropped_to_usb += len - written;
    xSemaphoreGive(s_lock);

    return written;
}

size_t terminal_bridge_read_input_for_usb(uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    if (data == NULL || len == 0 || terminal_bridge_start() != ESP_OK) {
        return 0;
    }

    return xStreamBufferReceive(s_to_usb_stream, data, len, timeout_ticks);
}

size_t terminal_bridge_snapshot_recent_output(uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || terminal_bridge_start() != ESP_OK) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t copied = terminal_scrollback_snapshot(&s_scrollback, data, len);
    xSemaphoreGive(s_lock);
    return copied;
}

terminal_bridge_status_t terminal_bridge_get_status(void)
{
    terminal_bridge_status_t status = {0};
    if (terminal_bridge_start() != ESP_OK) {
        return status;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    status = s_status;
    xSemaphoreGive(s_lock);
    return status;
}
