/* Linux stand-in for the hardware component, built against its real header. */

#include <string.h>

#include "terminal_bridge.h"

/* ---- terminal bridge: echoes input back as if the server typed it ---------- */

static terminal_bridge_output_cb_t s_output_cb;
static void *s_output_ctx;

esp_err_t terminal_bridge_start(void)
{
    return ESP_OK;
}

esp_err_t terminal_bridge_register_output_callback(terminal_bridge_output_cb_t callback, void *ctx)
{
    s_output_cb = callback;
    s_output_ctx = ctx;
    return ESP_OK;
}

size_t terminal_bridge_publish_usb_output(const uint8_t *data, size_t len)
{
    if (s_output_cb != NULL) {
        s_output_cb(data, len, s_output_ctx);
    }
    return len;
}

size_t terminal_bridge_submit_input(const uint8_t *data, size_t len)
{
    return terminal_bridge_publish_usb_output(data, len);
}

size_t terminal_bridge_read_input_for_usb(uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    (void)data;
    (void)len;
    (void)timeout_ticks;
    return 0;
}

size_t terminal_bridge_snapshot_recent_output(uint8_t *data, size_t len)
{
    static const char banner[] = "harness login: ";
    const size_t n = sizeof(banner) - 1 < len ? sizeof(banner) - 1 : len;
    memcpy(data, banner, n);
    return n;
}

terminal_bridge_status_t terminal_bridge_get_status(void)
{
    return (terminal_bridge_status_t){
        .bytes_from_usb = 123,
        .bytes_to_usb = 45,
        .scrollback_retained = 15,
        .scrollback_capacity = 8192,
    };
}
