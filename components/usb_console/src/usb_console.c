#include "usb_console.h"

#include "app_watchdog.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "terminal_bridge.h"

static const char *TAG = "usb_console";

#define USB_CONSOLE_BUF_SIZE 256
#define USB_CONSOLE_TASK_STACK 4096
#define USB_CONSOLE_RX_TASK_PRIORITY 8
#define USB_CONSOLE_TX_TASK_PRIORITY 8
#define USB_CONSOLE_WRITE_TIMEOUT_TICKS pdMS_TO_TICKS(5)

static usb_console_status_t s_status = {
    .connected = false,
    .bytes_received = 0,
    .bytes_sent = 0,
};
static SemaphoreHandle_t s_status_lock;
static StaticSemaphore_t s_status_lock_storage;
static bool s_started;

static void update_connected(void)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.connected = usb_serial_jtag_is_connected();
    xSemaphoreGive(s_status_lock);
}

static void add_received(size_t len)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.connected = usb_serial_jtag_is_connected();
    s_status.bytes_received += len;
    xSemaphoreGive(s_status_lock);
}

static void add_sent(size_t len)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.connected = usb_serial_jtag_is_connected();
    s_status.bytes_sent += len;
    xSemaphoreGive(s_status_lock);
}

static void usb_rx_task(void *arg)
{
    (void)arg;
    app_watchdog_register_current_task("usb_rx");
    uint8_t buf[USB_CONSOLE_BUF_SIZE];
    while (true) {
        app_watchdog_reset_current_task();
        const int read = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (read > 0) {
            add_received((size_t)read);
            terminal_bridge_publish_usb_output(buf, (size_t)read);
        } else {
            update_connected();
        }
    }
}

static void usb_tx_task(void *arg)
{
    (void)arg;
    app_watchdog_register_current_task("usb_tx");
    uint8_t buf[USB_CONSOLE_BUF_SIZE];
    while (true) {
        app_watchdog_reset_current_task();
        const size_t read = terminal_bridge_read_input_for_usb(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (read == 0) {
            update_connected();
            continue;
        }

        size_t offset = 0;
        while (offset < read) {
            const int written = usb_serial_jtag_write_bytes(
                buf + offset,
                read - offset,
                USB_CONSOLE_WRITE_TIMEOUT_TICKS);
            if (written <= 0) {
                ESP_LOGW(TAG, "USB TX backpressure; dropped %u bytes", (unsigned)(read - offset));
                break;
            }
            offset += (size_t)written;
            add_sent((size_t)written);
        }
    }
}

esp_err_t usb_console_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(terminal_bridge_start(), TAG, "bridge start failed");

    s_status_lock = xSemaphoreCreateMutexStatic(&s_status_lock_storage);
    ESP_RETURN_ON_FALSE(s_status_lock != NULL, ESP_ERR_NO_MEM, TAG, "status lock create failed");

    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 2048,
    };
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&config), TAG, "usb_serial_jtag driver install failed");

    BaseType_t rx_ok = xTaskCreate(
        usb_rx_task,
        "usb_rx",
        USB_CONSOLE_TASK_STACK,
        NULL,
        USB_CONSOLE_RX_TASK_PRIORITY,
        NULL);
    ESP_RETURN_ON_FALSE(rx_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "usb rx task create failed");

    BaseType_t tx_ok = xTaskCreate(
        usb_tx_task,
        "usb_tx",
        USB_CONSOLE_TASK_STACK,
        NULL,
        USB_CONSOLE_TX_TASK_PRIORITY,
        NULL);
    ESP_RETURN_ON_FALSE(tx_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "usb tx task create failed");

    update_connected();
    s_started = true;
    ESP_LOGI(TAG, "USB Serial/JTAG CDC bridge started");
    return ESP_OK;
}

usb_console_status_t usb_console_get_status(void)
{
    if (s_status_lock == NULL) {
        return s_status;
    }

    usb_console_status_t status;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    status = s_status;
    xSemaphoreGive(s_status_lock);
    return status;
}

size_t usb_console_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || !s_started) {
        return 0;
    }

    const int written = usb_serial_jtag_write_bytes(data, len, USB_CONSOLE_WRITE_TIMEOUT_TICKS);
    if (written > 0) {
        add_sent((size_t)written);
        return (size_t)written;
    }
    return 0;
}
