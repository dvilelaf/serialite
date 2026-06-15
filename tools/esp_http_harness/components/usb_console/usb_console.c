#include "usb_console.h"
esp_err_t usb_console_start(void) { return ESP_OK; }
usb_console_status_t usb_console_get_status(void) { return (usb_console_status_t){.connected = true, .bytes_received = 123, .bytes_sent = 45}; }
size_t usb_console_write(const uint8_t *data, size_t len) { (void)data; return len; }
