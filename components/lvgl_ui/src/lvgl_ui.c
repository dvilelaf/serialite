#include "lvgl_ui.h"

#include "esp_log.h"

static const char *TAG = "lvgl_ui";

esp_err_t lvgl_ui_start(void)
{
    ESP_LOGW(TAG, "LVGL display driver not enabled yet");
    return ESP_OK;
}
