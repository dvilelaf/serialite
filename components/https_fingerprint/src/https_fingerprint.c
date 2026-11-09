#include "https_fingerprint.h"

#include <stdio.h>

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
