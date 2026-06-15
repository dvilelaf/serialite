#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { bool initialized; bool in_progress; bool pending_reboot; size_t expected_bytes; size_t written_bytes; char target_label[17]; esp_err_t last_error; } ota_update_status_t;
typedef struct { bool active; void *internal; } ota_update_session_t;
esp_err_t ota_update_init(void);
ota_update_status_t ota_update_get_status(void);
esp_err_t ota_update_mark_running_app_valid(void);
esp_err_t ota_update_begin(size_t content_len, ota_update_session_t *session);
esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t len);
esp_err_t ota_update_finish(ota_update_session_t *session);
void ota_update_abort(ota_update_session_t *session);
