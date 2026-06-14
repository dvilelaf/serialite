#include "ota_update.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ota_update_policy.h"

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    size_t expected_bytes;
    size_t written_bytes;
} ota_update_internal_session_t;

static const char *TAG = "ota_update";

static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static ota_update_status_t s_status;
static ota_update_internal_session_t s_session;

static void lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

esp_err_t ota_update_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex allocation failed");
    }

    lock();
    if (!s_status.initialized) {
        memset(&s_status, 0, sizeof(s_status));
        memset(&s_session, 0, sizeof(s_session));
        s_status.initialized = true;
        s_status.last_error = ESP_OK;
    }
    unlock();
    return ESP_OK;
}

ota_update_status_t ota_update_get_status(void)
{
    ota_update_status_t snapshot = {0};
    lock();
    snapshot = s_status;
    unlock();
    return snapshot;
}

esp_err_t ota_update_mark_running_app_valid(void)
{
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "running app marked valid for OTA rollback");
        return ESP_OK;
    }
    if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "could not mark running app valid: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ota_update_begin(size_t content_len, ota_update_session_t *session)
{
    ESP_RETURN_ON_ERROR(ota_update_init(), TAG, "ota init failed");
    ESP_RETURN_ON_FALSE(session != NULL, ESP_ERR_INVALID_ARG, TAG, "missing session");
    memset(session, 0, sizeof(*session));

    lock();
    const ota_update_policy_result_t policy = ota_update_policy_evaluate(content_len, s_status.in_progress);
    if (policy != OTA_UPDATE_POLICY_ACCEPT) {
        s_status.last_error = policy == OTA_UPDATE_POLICY_REJECT_BUSY ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE;
        unlock();
        return s_status.last_error;
    }
    s_status.in_progress = true;
    s_status.pending_reboot = false;
    s_status.expected_bytes = content_len;
    s_status.written_bytes = 0;
    s_status.target_label[0] = '\0';
    s_status.last_error = ESP_OK;
    unlock();

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || content_len > partition->size) {
        lock();
        s_status.in_progress = false;
        s_status.last_error = ESP_ERR_NOT_FOUND;
        unlock();
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(partition, content_len, &handle);
    if (err != ESP_OK) {
        lock();
        s_status.in_progress = false;
        s_status.last_error = err;
        unlock();
        return err;
    }

    lock();
    memset(&s_session, 0, sizeof(s_session));
    s_session.handle = handle;
    s_session.partition = partition;
    s_session.expected_bytes = content_len;
    strlcpy(s_status.target_label, partition->label, sizeof(s_status.target_label));
    session->active = true;
    session->internal = &s_session;
    unlock();
    return ESP_OK;
}

esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(session != NULL && session->active && session->internal == &s_session, ESP_ERR_INVALID_ARG, TAG, "invalid session");
    if (len == 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "missing data");

    lock();
    const esp_ota_handle_t handle = s_session.handle;
    const size_t new_total = s_session.written_bytes + len;
    if (new_total > s_session.expected_bytes) {
        s_status.last_error = ESP_ERR_INVALID_SIZE;
        unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    unlock();

    const esp_err_t err = esp_ota_write(handle, data, len);
    lock();
    if (err == ESP_OK) {
        s_session.written_bytes = new_total;
        s_status.written_bytes = new_total;
    } else {
        s_status.last_error = err;
    }
    unlock();
    return err;
}

esp_err_t ota_update_finish(ota_update_session_t *session)
{
    ESP_RETURN_ON_FALSE(session != NULL && session->active && session->internal == &s_session, ESP_ERR_INVALID_ARG, TAG, "invalid session");

    lock();
    const bool length_ok = s_session.written_bytes == s_session.expected_bytes;
    const esp_ota_handle_t handle = s_session.handle;
    const esp_partition_t *partition = s_session.partition;
    unlock();

    if (!length_ok) {
        esp_ota_abort(handle);
        lock();
        s_status.in_progress = false;
        s_status.last_error = ESP_ERR_INVALID_SIZE;
        memset(&s_session, 0, sizeof(s_session));
        session->active = false;
        session->internal = NULL;
        unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_end(handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(partition);
    }

    lock();
    s_status.in_progress = false;
    s_status.pending_reboot = err == ESP_OK;
    s_status.last_error = err;
    memset(&s_session, 0, sizeof(s_session));
    session->active = false;
    session->internal = NULL;
    unlock();
    return err;
}

void ota_update_abort(ota_update_session_t *session)
{
    if (session == NULL || !session->active || session->internal != &s_session) {
        return;
    }

    lock();
    const esp_ota_handle_t handle = s_session.handle;
    s_status.in_progress = false;
    s_status.last_error = ESP_ERR_INVALID_RESPONSE;
    memset(&s_session, 0, sizeof(s_session));
    session->active = false;
    session->internal = NULL;
    unlock();
    esp_ota_abort(handle);
}
