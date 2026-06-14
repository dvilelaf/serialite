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
    if (digest == NULL || digest_len != HTTPS_FINGERPRINT_SHA256_LEN || out == NULL) {
        return HTTPS_FINGERPRINT_ERR_INVALID_ARG;
    }
    if (out_size < HTTPS_FINGERPRINT_TEXT_LEN) {
        return HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL;
    }

    size_t offset = 0;
    for (size_t i = 0; i < digest_len; ++i) {
        const int written = snprintf(
            out + offset,
            out_size - offset,
            i + 1U == digest_len ? "%02X" : "%02X:",
            digest[i]);
        if (written < 0 || (size_t)written >= out_size - offset) {
            out[0] = '\0';
            return HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL;
        }
        offset += (size_t)written;
    }
    return HTTPS_FINGERPRINT_OK;
}

https_fingerprint_policy_result_t https_fingerprint_policy_can_start_listener(const https_fingerprint_policy_request_t *request)
{
    if (request == NULL || !request->requested) {
        return HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED;
    }
    if (!request->certificate_present) {
        return HTTPS_FINGERPRINT_POLICY_REJECT_NO_CERT;
    }
    if (!request->fingerprint_displayed_locally) {
        return HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED;
    }
    return HTTPS_FINGERPRINT_POLICY_ALLOW;
}

https_fingerprint_policy_result_t https_fingerprint_policy_can_enable(const https_fingerprint_policy_request_t *request)
{
    const https_fingerprint_policy_result_t listener_result = https_fingerprint_policy_can_start_listener(request);
    if (listener_result != HTTPS_FINGERPRINT_POLICY_ALLOW) {
        return listener_result;
    }
    if (!request->operator_acknowledged_fingerprint) {
        return HTTPS_FINGERPRINT_POLICY_REJECT_NO_ACK;
    }
    return HTTPS_FINGERPRINT_POLICY_ALLOW;
}

const char *https_fingerprint_policy_result_name(https_fingerprint_policy_result_t result)
{
    switch (result) {
        case HTTPS_FINGERPRINT_POLICY_ALLOW:
            return "allow";
        case HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED:
            return "disabled";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NO_CERT:
            return "no_cert";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED:
            return "not_displayed";
        case HTTPS_FINGERPRINT_POLICY_REJECT_NO_ACK:
            return "no_ack";
        default:
            return "unknown";
    }
}
