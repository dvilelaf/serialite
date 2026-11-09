#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Must match the ota_0/ota_1 size in partitions.csv; scripts/lint_production_profile.py checks it. */
#define OTA_UPDATE_MAX_IMAGE_BYTES 0x600000U
#define OTA_UPDATE_RECV_TIMEOUT_LIMIT 8U
#define OTA_UPDATE_UPLOAD_DEADLINE_MS 300000ULL

/*
 * The app descriptor sits right after the 24-byte image header and the first
 * 8-byte segment header. ota_update.c checks these offsets against ESP-IDF.
 */
#define OTA_UPDATE_APP_DESC_OFFSET 32U
#define OTA_UPDATE_APP_DESC_MAGIC 0xABCD5432U
#define OTA_UPDATE_APP_DESC_PROJECT_OFFSET 48U
#define OTA_UPDATE_APP_DESC_PROJECT_LEN 32U
#define OTA_UPDATE_IMAGE_HEAD_BYTES (OTA_UPDATE_APP_DESC_OFFSET + OTA_UPDATE_APP_DESC_PROJECT_OFFSET + OTA_UPDATE_APP_DESC_PROJECT_LEN)

typedef enum {
    OTA_UPDATE_POLICY_ACCEPT = 0,
    OTA_UPDATE_POLICY_REJECT_EMPTY,
    OTA_UPDATE_POLICY_REJECT_TOO_LARGE,
    OTA_UPDATE_POLICY_REJECT_BUSY,
    OTA_UPDATE_POLICY_REJECT_NOT_APP_IMAGE,
    OTA_UPDATE_POLICY_REJECT_WRONG_PROJECT,
} ota_update_policy_result_t;

ota_update_policy_result_t ota_update_policy_evaluate(size_t content_len, bool update_in_progress);
/* Checks the first OTA_UPDATE_IMAGE_HEAD_BYTES of an upload against the running project name. */
ota_update_policy_result_t ota_update_policy_check_image_head(const uint8_t *head, size_t len, const char *expected_project);
bool ota_update_policy_reboot_allowed(bool pending_reboot, bool update_in_progress);
bool ota_update_policy_timeout_exhausted(unsigned consecutive_timeouts);
bool ota_update_policy_deadline_exceeded(uint64_t start_ms, uint64_t now_ms);
const char *ota_update_policy_result_name(ota_update_policy_result_t result);
