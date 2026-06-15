#include "esp_https_server.h"

esp_err_t httpd_ssl_start(httpd_handle_t *handle, const httpd_ssl_config_t *config)
{
    (void)handle;
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t httpd_ssl_stop(httpd_handle_t handle)
{
    return httpd_stop(handle);
}
