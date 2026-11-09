#pragma once

#include <stddef.h>
#include <stdint.h>

#define HTTPS_FINGERPRINT_SHA256_LEN 32U
/* 32 bytes as "AA:BB:..": 95 characters plus the terminator. */
#define HTTPS_FINGERPRINT_TEXT_LEN 96U

typedef enum {
    HTTPS_FINGERPRINT_OK = 0,
    HTTPS_FINGERPRINT_ERR_INVALID_ARG,
    HTTPS_FINGERPRINT_ERR_OUTPUT_TOO_SMALL,
} https_fingerprint_result_t;

/* Formats a certificate SHA-256 the way browsers show it. */
https_fingerprint_result_t https_fingerprint_format_sha256(
    const uint8_t *digest,
    size_t digest_len,
    char *out,
    size_t out_size);
