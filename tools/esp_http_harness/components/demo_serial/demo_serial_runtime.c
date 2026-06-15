#include "demo_serial_runtime.h"
esp_err_t demo_serial_runtime_start(void) { return ESP_OK; }
esp_err_t demo_serial_runtime_enable(bool writer_active) { (void)writer_active; return ESP_OK; }
void demo_serial_runtime_disable(void) {}
demo_serial_runtime_status_t demo_serial_runtime_get_status(void) { return (demo_serial_runtime_status_t){0}; }
