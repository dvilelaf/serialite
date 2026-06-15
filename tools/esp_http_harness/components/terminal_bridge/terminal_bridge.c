#include "terminal_bridge.h"
#include <string.h>
static terminal_bridge_output_cb_t s_cb;
static void *s_ctx;
esp_err_t terminal_bridge_start(void) { return ESP_OK; }
esp_err_t terminal_bridge_register_output_callback(terminal_bridge_output_cb_t callback, void *ctx) { s_cb = callback; s_ctx = ctx; return ESP_OK; }
size_t terminal_bridge_publish_usb_output(const uint8_t *data, size_t len) { if (s_cb) s_cb(data, len, s_ctx); return len; }
size_t terminal_bridge_submit_input(terminal_bridge_source_t source, const uint8_t *data, size_t len) { (void)source; terminal_bridge_publish_usb_output(data, len); return len; }
size_t terminal_bridge_read_input_for_usb(uint8_t *data, size_t len, TickType_t timeout_ticks) { (void)data; (void)len; (void)timeout_ticks; return 0; }
size_t terminal_bridge_snapshot_recent_output(uint8_t *data, size_t len) { const char msg[] = "harness login: "; size_t n = sizeof(msg) - 1; if (n > len) n = len; memcpy(data, msg, n); return n; }
terminal_bridge_status_t terminal_bridge_get_status(void) { return (terminal_bridge_status_t){.bytes_from_usb = 123, .bytes_to_usb = 45, .scrollback_retained = 15, .scrollback_capacity = 4096}; }
