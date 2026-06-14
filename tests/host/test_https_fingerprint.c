#include "https_fingerprint.h"

#include <assert.h>
#include <string.h>

static void https_is_disabled_by_default(void)
{
    assert(!https_fingerprint_default_enabled());
}

static void formats_sha256_fingerprint_as_uppercase_colon_hex(void)
{
    uint8_t digest[HTTPS_FINGERPRINT_SHA256_LEN];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        digest[i] = (uint8_t)i;
    }

    char out[HTTPS_FINGERPRINT_TEXT_LEN];
    assert(https_fingerprint_format_sha256(digest, sizeof(digest), out, sizeof(out)) == HTTPS_FINGERPRINT_OK);
    assert(strcmp(out, "00:01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F") == 0);
}

static void rejects_bad_fingerprint_inputs(void)
{
    uint8_t digest[HTTPS_FINGERPRINT_SHA256_LEN] = {0};
    char out[HTTPS_FINGERPRINT_TEXT_LEN];

    assert(https_fingerprint_format_sha256(NULL, sizeof(digest), out, sizeof(out)) == HTTPS_FINGERPRINT_ERR_INVALID_ARG);
    assert(https_fingerprint_format_sha256(digest, sizeof(digest) - 1U, out, sizeof(out)) == HTTPS_FINGERPRINT_ERR_INVALID_ARG);
    assert(https_fingerprint_format_sha256(digest, sizeof(digest), NULL, sizeof(out)) == HTTPS_FINGERPRINT_ERR_INVALID_ARG);
    assert(https_fingerprint_format_sha256(digest, sizeof(digest), out, sizeof(out) - 1U) == HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL);
}

static void enable_requires_fingerprint_local_display_and_operator_ack(void)
{
    assert(https_fingerprint_policy_can_enable(NULL) == HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED);

    https_fingerprint_policy_request_t req = {
        .requested = true,
        .certificate_present = true,
        .fingerprint_displayed_locally = true,
        .operator_acknowledged_fingerprint = true,
    };
    assert(https_fingerprint_policy_can_enable(&req) == HTTPS_FINGERPRINT_POLICY_ALLOW);

    req.requested = false;
    assert(https_fingerprint_policy_can_enable(&req) == HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED);

    req.requested = true;
    req.certificate_present = false;
    assert(https_fingerprint_policy_can_enable(&req) == HTTPS_FINGERPRINT_POLICY_REJECT_NO_CERT);

    req.certificate_present = true;
    req.fingerprint_displayed_locally = false;
    assert(https_fingerprint_policy_can_enable(&req) == HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED);

    req.fingerprint_displayed_locally = true;
    req.operator_acknowledged_fingerprint = false;
    assert(https_fingerprint_policy_can_enable(&req) == HTTPS_FINGERPRINT_POLICY_REJECT_NO_ACK);
}

static void policy_result_names_are_stable(void)
{
    assert(strcmp(https_fingerprint_policy_result_name(HTTPS_FINGERPRINT_POLICY_ALLOW), "allow") == 0);
    assert(strcmp(https_fingerprint_policy_result_name(HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED), "not_displayed") == 0);
    assert(strcmp(https_fingerprint_policy_result_name((https_fingerprint_policy_result_t)999), "unknown") == 0);
}

int main(void)
{
    https_is_disabled_by_default();
    formats_sha256_fingerprint_as_uppercase_colon_hex();
    rejects_bad_fingerprint_inputs();
    enable_requires_fingerprint_local_display_and_operator_ack();
    policy_result_names_are_stable();
    return 0;
}
