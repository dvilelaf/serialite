#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { bool active; uint64_t bytes_emitted; } demo_serial_runtime_status_t;
esp_err_t demo_serial_runtime_start(void);
esp_err_t demo_serial_runtime_enable(bool writer_active);
void demo_serial_runtime_disable(void);
demo_serial_runtime_status_t demo_serial_runtime_get_status(void);
