#pragma once

#include "https_fingerprint.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOCAL_TLS_IDENTITY_CERT_PEM_MAX 2048U
#define LOCAL_TLS_IDENTITY_KEY_PEM_MAX 1024U
#define LOCAL_TLS_IDENTITY_COMMON_NAME_MAX 64U

typedef bool (*local_tls_identity_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef enum {
    LOCAL_TLS_IDENTITY_OK = 0,
    LOCAL_TLS_IDENTITY_ERR_INVALID_ARG,
    LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED,
    LOCAL_TLS_IDENTITY_ERR_KEYGEN_FAILED,
    LOCAL_TLS_IDENTITY_ERR_CERT_FAILED,
    LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED,
} local_tls_identity_result_t;

typedef struct {
    char cert_pem[LOCAL_TLS_IDENTITY_CERT_PEM_MAX];
    size_t cert_pem_len;
    char key_pem[LOCAL_TLS_IDENTITY_KEY_PEM_MAX];
    size_t key_pem_len;
    uint8_t fingerprint_sha256[HTTPS_FINGERPRINT_SHA256_LEN];
    char fingerprint_text[HTTPS_FINGERPRINT_TEXT_LEN];
} local_tls_identity_t;

local_tls_identity_result_t local_tls_identity_generate(
    local_tls_identity_t *identity,
    local_tls_identity_random_fn_t random_fn,
    void *random_ctx,
    const char *common_name);

void local_tls_identity_zeroize(local_tls_identity_t *identity);
const char *local_tls_identity_result_name(local_tls_identity_result_t result);
