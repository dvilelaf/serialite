#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_UPDATE_MAX_IMAGE_BYTES 0x600000U
#define OTA_UPDATE_RECV_TIMEOUT_LIMIT 8U
#define OTA_UPDATE_UPLOAD_DEADLINE_MS 300000ULL

typedef enum {
    OTA_UPDATE_POLICY_ACCEPT = 0,
    OTA_UPDATE_POLICY_REJECT_EMPTY,
    OTA_UPDATE_POLICY_REJECT_TOO_LARGE,
    OTA_UPDATE_POLICY_REJECT_BUSY,
} ota_update_policy_result_t;

ota_update_policy_result_t ota_update_policy_evaluate(size_t content_len, bool update_in_progress);
bool ota_update_policy_reboot_allowed(bool pending_reboot, bool update_in_progress);
bool ota_update_policy_timeout_exhausted(unsigned consecutive_timeouts);
bool ota_update_policy_deadline_exceeded(uint64_t start_ms, uint64_t now_ms);
const char *ota_update_policy_result_name(ota_update_policy_result_t result);
