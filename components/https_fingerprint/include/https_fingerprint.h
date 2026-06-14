#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTPS_FINGERPRINT_SHA256_LEN 32U
#define HTTPS_FINGERPRINT_TEXT_LEN 96U

typedef enum {
    HTTPS_FINGERPRINT_OK = 0,
    HTTPS_FINGERPRINT_ERR_INVALID_ARG,
    HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL,
} https_fingerprint_result_t;

typedef enum {
    HTTPS_FINGERPRINT_POLICY_REJECT_DISABLED = 0,
    HTTPS_FINGERPRINT_POLICY_ALLOW,
    HTTPS_FINGERPRINT_POLICY_REJECT_NO_CERT,
    HTTPS_FINGERPRINT_POLICY_REJECT_NOT_DISPLAYED,
    HTTPS_FINGERPRINT_POLICY_REJECT_NO_ACK,
} https_fingerprint_policy_result_t;

typedef struct {
    bool requested;
    bool certificate_present;
    bool fingerprint_displayed_locally;
    bool operator_acknowledged_fingerprint;
} https_fingerprint_policy_request_t;

bool https_fingerprint_default_enabled(void);
https_fingerprint_result_t https_fingerprint_format_sha256(
    const uint8_t *digest,
    size_t digest_len,
    char *out,
    size_t out_size);
https_fingerprint_policy_result_t https_fingerprint_policy_can_enable(const https_fingerprint_policy_request_t *request);
const char *https_fingerprint_policy_result_name(https_fingerprint_policy_result_t result);
