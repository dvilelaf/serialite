#include "ota_update_policy.h"

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
        default:
            return "unknown";
    }
}
