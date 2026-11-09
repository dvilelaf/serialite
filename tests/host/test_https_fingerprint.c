#include "https_fingerprint.h"

#include <assert.h>
#include <string.h>

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

int main(void)
{
    formats_sha256_fingerprint_as_uppercase_colon_hex();
    rejects_bad_fingerprint_inputs();
    return 0;
}
