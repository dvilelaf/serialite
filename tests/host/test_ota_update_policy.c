#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ota_update_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_accepts_non_empty_image_within_ota_slot(void)
{
    CHECK(ota_update_policy_evaluate(1, false) == OTA_UPDATE_POLICY_ACCEPT);
    CHECK(ota_update_policy_evaluate(OTA_UPDATE_MAX_IMAGE_BYTES, false) == OTA_UPDATE_POLICY_ACCEPT);
}

static void test_rejects_empty_oversized_or_concurrent_updates(void)
{
    CHECK(ota_update_policy_evaluate(0, false) == OTA_UPDATE_POLICY_REJECT_EMPTY);
    CHECK(ota_update_policy_evaluate(OTA_UPDATE_MAX_IMAGE_BYTES + 1, false) == OTA_UPDATE_POLICY_REJECT_TOO_LARGE);
    CHECK(ota_update_policy_evaluate(4096, true) == OTA_UPDATE_POLICY_REJECT_BUSY);
}

static void test_result_names_are_stable_for_audit_logs(void)
{
    CHECK(strcmp(ota_update_policy_result_name(OTA_UPDATE_POLICY_ACCEPT), "accept") == 0);
    CHECK(strcmp(ota_update_policy_result_name(OTA_UPDATE_POLICY_REJECT_EMPTY), "empty") == 0);
    CHECK(strcmp(ota_update_policy_result_name(OTA_UPDATE_POLICY_REJECT_TOO_LARGE), "too_large") == 0);
    CHECK(strcmp(ota_update_policy_result_name(OTA_UPDATE_POLICY_REJECT_BUSY), "busy") == 0);
    CHECK(strcmp(ota_update_policy_result_name((ota_update_policy_result_t)99), "unknown") == 0);
}

static void test_reboot_requires_pending_image_and_no_active_upload(void)
{
    CHECK(ota_update_policy_reboot_allowed(true, false));
    CHECK(!ota_update_policy_reboot_allowed(false, false));
    CHECK(!ota_update_policy_reboot_allowed(true, true));
}

static void test_recv_timeout_retry_has_a_finite_limit(void)
{
    CHECK(!ota_update_policy_timeout_exhausted(0));
    CHECK(!ota_update_policy_timeout_exhausted(OTA_UPDATE_RECV_TIMEOUT_LIMIT - 1));
    CHECK(ota_update_policy_timeout_exhausted(OTA_UPDATE_RECV_TIMEOUT_LIMIT));
    CHECK(ota_update_policy_timeout_exhausted(OTA_UPDATE_RECV_TIMEOUT_LIMIT + 1));
}

static void test_upload_deadline_limits_slow_trickle_clients(void)
{
    CHECK(!ota_update_policy_deadline_exceeded(1000, 1000 + OTA_UPDATE_UPLOAD_DEADLINE_MS));
    CHECK(ota_update_policy_deadline_exceeded(1000, 1001 + OTA_UPDATE_UPLOAD_DEADLINE_MS));
    CHECK(!ota_update_policy_deadline_exceeded(1000, 999));
}

int main(void)
{
    test_accepts_non_empty_image_within_ota_slot();
    test_rejects_empty_oversized_or_concurrent_updates();
    test_result_names_are_stable_for_audit_logs();
    test_reboot_requires_pending_image_and_no_active_upload();
    test_recv_timeout_retry_has_a_finite_limit();
    test_upload_deadline_limits_slow_trickle_clients();
    return 0;
}
