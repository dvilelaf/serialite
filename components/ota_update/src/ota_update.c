#include "ota_update.h"

#include <stddef.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ota_update_policy.h"

_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == OTA_UPDATE_APP_DESC_OFFSET,
               "app descriptor offset changed");
_Static_assert(offsetof(esp_app_desc_t, project_name) == OTA_UPDATE_APP_DESC_PROJECT_OFFSET,
               "project_name offset changed");
_Static_assert(sizeof(((esp_app_desc_t *)0)->project_name) == OTA_UPDATE_APP_DESC_PROJECT_LEN,
               "project_name size changed");
_Static_assert(ESP_APP_DESC_MAGIC_WORD == OTA_UPDATE_APP_DESC_MAGIC, "app descriptor magic changed");

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    size_t expected_bytes;
    size_t written_bytes;
    /* The image head is held back until its app descriptor has been checked. */
    uint8_t head[OTA_UPDATE_IMAGE_HEAD_BYTES];
    size_t head_len;
    bool head_checked;
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

static bool session_valid(const ota_update_session_t *session)
{
    return session != NULL && session->active && session->internal == &s_session;
}

static void end_session(ota_update_session_t *session, esp_err_t result, bool pending_reboot)
{
    lock();
    s_status.in_progress = false;
    s_status.pending_reboot = pending_reboot;
    s_status.last_error = result;
    memset(&s_session, 0, sizeof(s_session));
    session->active = false;
    session->internal = NULL;
    unlock();
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
        const esp_err_t err = s_status.last_error;
        unlock();
        return err;
    }
    s_status.in_progress = true;
    s_status.pending_reboot = false;
    s_status.expected_bytes = content_len;
    s_status.written_bytes = 0;
    s_status.target_label[0] = '\0';
    s_status.last_error = ESP_OK;
    unlock();

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    esp_err_t err = partition == NULL || content_len > partition->size ? ESP_ERR_NOT_FOUND : ESP_OK;
    esp_ota_handle_t handle = 0;
    if (err == ESP_OK) {
        err = esp_ota_begin(partition, content_len, &handle);
    }
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

static esp_err_t check_and_flush_head(void)
{
    const ota_update_policy_result_t policy = ota_update_policy_check_image_head(
        s_session.head,
        s_session.head_len,
        esp_app_get_description()->project_name);
    if (policy != OTA_UPDATE_POLICY_ACCEPT) {
        ESP_LOGW(TAG, "upload rejected: %s", ota_update_policy_result_name(policy));
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_session.head_checked = true;
    return esp_ota_write(s_session.handle, s_session.head, s_session.head_len);
}

esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(session_valid(session), ESP_ERR_INVALID_ARG, TAG, "invalid session");
    if (len == 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "missing data");
    if (s_session.written_bytes + len > s_session.expected_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    size_t remaining = len;
    esp_err_t err = ESP_OK;
    if (!s_session.head_checked) {
        const size_t room = sizeof(s_session.head) - s_session.head_len;
        const size_t take = remaining < room ? remaining : room;
        memcpy(s_session.head + s_session.head_len, bytes, take);
        s_session.head_len += take;
        bytes += take;
        remaining -= take;
        if (s_session.head_len == sizeof(s_session.head)) {
            err = check_and_flush_head();
        }
    }
    if (err == ESP_OK && remaining > 0) {
        err = esp_ota_write(s_session.handle, bytes, remaining);
    }

    lock();
    if (err == ESP_OK) {
        s_session.written_bytes += len;
        s_status.written_bytes = s_session.written_bytes;
    } else {
        s_status.last_error = err;
    }
    unlock();
    return err;
}

esp_err_t ota_update_finish(ota_update_session_t *session)
{
    ESP_RETURN_ON_FALSE(session_valid(session), ESP_ERR_INVALID_ARG, TAG, "invalid session");

    if (!s_session.head_checked || s_session.written_bytes != s_session.expected_bytes) {
        esp_ota_abort(s_session.handle);
        end_session(session, ESP_ERR_INVALID_SIZE, false);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *partition = s_session.partition;
    esp_err_t err = esp_ota_end(s_session.handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(partition);
    }
    end_session(session, err, err == ESP_OK);
    return err;
}

void ota_update_abort(ota_update_session_t *session, esp_err_t reason)
{
    if (!session_valid(session)) {
        return;
    }
    const esp_ota_handle_t handle = s_session.handle;
    end_session(session, reason, false);
    esp_ota_abort(handle);
}
