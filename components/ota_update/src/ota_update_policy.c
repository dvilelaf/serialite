#include "ota_update_policy.h"

#include <string.h>

#define IMAGE_HEADER_MAGIC 0xE9U

ota_update_policy_result_t ota_update_policy_evaluate(size_t content_len, bool update_in_progress)
{
    if (update_in_progress) {
        return OTA_UPDATE_POLICY_REJECT_BUSY;
    }
    if (content_len == 0) {
        return OTA_UPDATE_POLICY_REJECT_EMPTY;
    }
    if (content_len > OTA_UPDATE_MAX_IMAGE_BYTES) {
        return OTA_UPDATE_POLICY_REJECT_TOO_LARGE;
    }
    return OTA_UPDATE_POLICY_ACCEPT;
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

ota_update_policy_result_t ota_update_policy_check_image_head(const uint8_t *head, size_t len, const char *expected_project)
{
    if (head == NULL || expected_project == NULL || len < OTA_UPDATE_IMAGE_HEAD_BYTES ||
        head[0] != IMAGE_HEADER_MAGIC ||
        read_le32(head + OTA_UPDATE_APP_DESC_OFFSET) != OTA_UPDATE_APP_DESC_MAGIC) {
        return OTA_UPDATE_POLICY_REJECT_NOT_APP_IMAGE;
    }

    const char *project = (const char *)head + OTA_UPDATE_APP_DESC_OFFSET + OTA_UPDATE_APP_DESC_PROJECT_OFFSET;
    const size_t project_len = strnlen(project, OTA_UPDATE_APP_DESC_PROJECT_LEN);
    if (project_len == OTA_UPDATE_APP_DESC_PROJECT_LEN ||
        project_len != strlen(expected_project) ||
        memcmp(project, expected_project, project_len) != 0) {
        return OTA_UPDATE_POLICY_REJECT_WRONG_PROJECT;
    }
    return OTA_UPDATE_POLICY_ACCEPT;
}

bool ota_update_policy_reboot_allowed(bool pending_reboot, bool update_in_progress)
{
    return pending_reboot && !update_in_progress;
}

bool ota_update_policy_timeout_exhausted(unsigned consecutive_timeouts)
{
    return consecutive_timeouts >= OTA_UPDATE_RECV_TIMEOUT_LIMIT;
}

bool ota_update_policy_deadline_exceeded(uint64_t start_ms, uint64_t now_ms)
{
    return now_ms >= start_ms && (now_ms - start_ms) > OTA_UPDATE_UPLOAD_DEADLINE_MS;
}

const char *ota_update_policy_result_name(ota_update_policy_result_t result)
{
    switch (result) {
        case OTA_UPDATE_POLICY_ACCEPT:
            return "accept";
        case OTA_UPDATE_POLICY_REJECT_EMPTY:
            return "empty";
        case OTA_UPDATE_POLICY_REJECT_TOO_LARGE:
            return "too_large";
        case OTA_UPDATE_POLICY_REJECT_BUSY:
            return "busy";
        case OTA_UPDATE_POLICY_REJECT_NOT_APP_IMAGE:
            return "not_app_image";
        case OTA_UPDATE_POLICY_REJECT_WRONG_PROJECT:
            return "wrong_project";
        default:
            return "unknown";
    }
}
