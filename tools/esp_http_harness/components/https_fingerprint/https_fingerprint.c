#include "https_fingerprint.h"

#include <stdio.h>

bool https_fingerprint_default_enabled(void)
{
    return false;
}

https_fingerprint_result_t https_fingerprint_format_sha256(
    const uint8_t *digest,
    size_t digest_len,
    char *out,
    size_t out_size)
{
    if (digest == NULL || out == NULL || digest_len != HTTPS_FINGERPRINT_SHA256_LEN) {
        return HTTPS_FINGERPRINT_ERR_INVALID_ARG;
    }
    if (out_size < HTTPS_FINGERPRINT_TEXT_LEN) {
        return HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL;
    }
    size_t used = 0;
    for (size_t i = 0; i < digest_len; ++i) {
        const int n = snprintf(out + used, out_size - used, "%02X%s", digest[i], i + 1U == digest_len ? "" : ":");
        if (n < 0 || (size_t)n >= out_size - used) {
            return HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL;
        }
        used += (size_t)n;
    }
    return HTTPS_FINGERPRINT_OK;
}

https_fingerprint_policy_result_t https_fingerprint_policy_can_start_listener(const https_fingerprint_policy_request_t *request)
{
    (void)request;
    return HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED;
}

https_fingerprint_policy_result_t https_fingerprint_policy_can_enable(const https_fingerprint_policy_request_t *request)
{
    (void)request;
    return HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED;
}

const char *https_fingerprint_policy_result_name(https_fingerprint_policy_result_t result)
{
    switch (result) {
        case HTTPS_FINGERPRINT_POLICY_ALLOW:
            return "allow";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NO_CERT:
            return "no_cert";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED:
            return "not_displayed";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NO_ACK:
            return "no_ack";
        case HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED:
        default:
            return "disabled";
    }
}
