/* Boards without a display link this instead of the LVGL screen. */

#include "lvgl_ui.h"

esp_err_t lvgl_ui_start(const lvgl_ui_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

bool lvgl_ui_is_running(void)
{
    return false;
}

esp_err_t lvgl_ui_set_credentials(const lvgl_ui_credentials_t *credentials, bool reveal)
{
    (void)credentials;
    (void)reveal;
    return ESP_ERR_INVALID_STATE;
}

void lvgl_ui_notify_primary_press(void)
{
}

void lvgl_ui_notify_primary_hold(uint32_t held_ms, uint32_t reset_after_ms)
{
    (void)held_ms;
    (void)reset_after_ms;
}
