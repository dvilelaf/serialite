#include "ota_update.h"
esp_err_t ota_update_init(void) { return ESP_OK; }
ota_update_status_t ota_update_get_status(void) { return (ota_update_status_t){.initialized = true}; }
esp_err_t ota_update_mark_running_app_valid(void) { return ESP_OK; }
esp_err_t ota_update_begin(size_t content_len, ota_update_session_t *session) { (void)content_len; if (session) session->active = true; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t len) { (void)session; (void)data; (void)len; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ota_update_finish(ota_update_session_t *session) { (void)session; return ESP_ERR_NOT_SUPPORTED; }
void ota_update_abort(ota_update_session_t *session) { if (session) session->active = false; }
