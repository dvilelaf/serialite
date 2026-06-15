#pragma once
#include "esp_err.h"
esp_err_t app_watchdog_register_current_task(const char *task_name);
void app_watchdog_reset_current_task(void);
