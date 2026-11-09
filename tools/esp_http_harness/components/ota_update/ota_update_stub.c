/* Linux stand-in for the hardware component, built against its real header. */

#include <stdio.h>

#include "ota_update.h"

#define HARNESS_MARK(...) do { printf("HARNESS " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

/* ---- OTA: accepts bytes, never produces a bootable image -------------------- */

static ota_update_status_t s_ota = {.initialized = true};
static int s_ota_token;

esp_err_t ota_update_init(void)
{
    return ESP_OK;
}

ota_update_status_t ota_update_get_status(void)
{
    return s_ota;
}

esp_err_t ota_update_mark_running_app_valid(void)
{
    return ESP_OK;
}

esp_err_t ota_update_begin(size_t content_len, ota_update_session_t *session)
{
    HARNESS_MARK("ota_update_begin len=%zu", content_len);
    s_ota.in_progress = true;
    s_ota.expected_bytes = content_len;
    s_ota.written_bytes = 0;
    session->active = true;
    session->internal = &s_ota_token;
    return ESP_OK;
}

esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t len)
{
    (void)session;
    (void)data;
    s_ota.written_bytes += len;
    return ESP_OK;
}

esp_err_t ota_update_finish(ota_update_session_t *session)
{
    session->active = false;
    s_ota.in_progress = false;
    s_ota.last_error = ESP_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

void ota_update_abort(ota_update_session_t *session, esp_err_t reason)
{
    session->active = false;
    s_ota.in_progress = false;
    s_ota.last_error = reason;
}
