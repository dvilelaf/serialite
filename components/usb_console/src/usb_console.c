#include "usb_console.h"

#include "esp_log.h"

static const char *TAG = "usb_console";

static usb_console_status_t s_status = {
    .connected = false,
    .bytes_received = 0,
    .bytes_sent = 0,
};

esp_err_t usb_console_start(void)
{
    ESP_LOGW(TAG, "USB CDC ACM transport not enabled yet");
    return ESP_OK;
}

usb_console_status_t usb_console_get_status(void)
{
    return s_status;
}

size_t usb_console_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || !s_status.connected) {
        return 0;
    }

    return 0;
}
