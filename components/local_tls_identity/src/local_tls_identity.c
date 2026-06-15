#include "local_tls_identity.h"

#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/bignum.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    local_tls_identity_random_fn_t fn;
    void *ctx;
    bool failed;
} tls_rng_ctx_t;

static int tls_rng(void *ctx, unsigned char *out, size_t len)
{
    tls_rng_ctx_t *rng = (tls_rng_ctx_t *)ctx;
    if (rng == NULL || rng->fn == NULL || out == NULL || !rng->fn(out, len, rng->ctx)) {
        if (rng != NULL) {
            rng->failed = true;
        }
        return -1;
    }
    return 0;
}

static bool common_name_valid(const char *common_name)
{
    if (common_name == NULL) {
        return false;
    }
    size_t len = 0;
    while (common_name[len] != '\0') {
        const unsigned char c = (unsigned char)common_name[len];
        if (c < 0x20U || c >= 0x7fU || c == ',' || c == '=') {
            return false;
        }
        ++len;
        if (len >= LOCAL_TLS_IDENTITY_COMMON_NAME_MAX) {
            return false;
        }
    }
    return len > 0;
}

static void secure_zero(void *ptr, size_t len)
{
    if (ptr == NULL) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0U) {
        *p++ = 0;
    }
}

static local_tls_identity_result_t compute_cert_fingerprint(local_tls_identity_t *identity)
{
    mbedtls_x509_crt parsed;
    mbedtls_x509_crt_init(&parsed);
    const int parse_ret = mbedtls_x509_crt_parse(
        &parsed,
        (const unsigned char *)identity->cert_pem,
        identity->cert_pem_len + 1U);
    if (parse_ret != 0) {
        mbedtls_x509_crt_free(&parsed);
        return LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED;
    }

    const int sha_ret = mbedtls_sha256(parsed.raw.p, parsed.raw.len, identity->fingerprint_sha256, 0);
    mbedtls_x509_crt_free(&parsed);
    if (sha_ret != 0) {
        return LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED;
    }

    const https_fingerprint_result_t fmt_ret = https_fingerprint_format_sha256(
        identity->fingerprint_sha256,
        sizeof(identity->fingerprint_sha256),
        identity->fingerprint_text,
        sizeof(identity->fingerprint_text));
    return fmt_ret == HTTPS_FINGERPRINT_OK ? LOCAL_TLS_IDENTITY_OK : LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED;
}

local_tls_identity_result_t local_tls_identity_generate(
    local_tls_identity_t *identity,
    local_tls_identity_random_fn_t random_fn,
    void *random_ctx,
    const char *common_name)
{
    if (identity == NULL || random_fn == NULL || !common_name_valid(common_name)) {
        return LOCAL_TLS_IDENTITY_ERR_INVALID_ARG;
    }

    memset(identity, 0, sizeof(*identity));

    tls_rng_ctx_t rng = {
        .fn = random_fn,
        .ctx = random_ctx,
        .failed = false,
    };

    mbedtls_pk_context key;
    mbedtls_x509write_cert cert;
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&cert);

    local_tls_identity_result_t result = LOCAL_TLS_IDENTITY_ERR_KEYGEN_FAILED;
    int ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), tls_rng, &rng);
    if (ret != 0) {
        result = rng.failed ? LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED : LOCAL_TLS_IDENTITY_ERR_KEYGEN_FAILED;
        goto cleanup;
    }
    ret = mbedtls_pk_write_key_pem(&key, (unsigned char *)identity->key_pem, sizeof(identity->key_pem));
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_KEYGEN_FAILED;
        goto cleanup;
    }
    identity->key_pem_len = strlen(identity->key_pem);

    mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&cert, &key);
    mbedtls_x509write_crt_set_issuer_key(&cert, &key);

    uint8_t serial[16];
    if (tls_rng(&rng, serial, sizeof(serial)) != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED;
        goto cleanup;
    }
    serial[0] &= 0x7fU;
    serial[sizeof(serial) - 1U] |= 0x01U;
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    ret = mbedtls_x509write_crt_set_serial_raw(&cert, serial, sizeof(serial));
    secure_zero(serial, sizeof(serial));
#else
    mbedtls_mpi serial_mpi;
    mbedtls_mpi_init(&serial_mpi);
    ret = mbedtls_mpi_read_binary(&serial_mpi, serial, sizeof(serial));
    secure_zero(serial, sizeof(serial));
    if (ret == 0) {
        ret = mbedtls_x509write_crt_set_serial(&cert, &serial_mpi);
    }
    mbedtls_mpi_free(&serial_mpi);
#endif
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }

    char subject[96];
    const int subject_ret = snprintf(subject, sizeof(subject), "CN=%s,O=Serialite", common_name);
    if (subject_ret < 0 || subject_ret >= (int)sizeof(subject)) {
        result = LOCAL_TLS_IDENTITY_ERR_INVALID_ARG;
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_subject_name(&cert, subject);
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_issuer_name(&cert, subject);
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    mbedtls_x509_san_list san = {0};
    san.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    san.node.san.unstructured_name.p = (unsigned char *)common_name;
    san.node.san.unstructured_name.len = strlen(common_name);
    ret = mbedtls_x509write_crt_set_subject_alternative_name(&cert, &san);
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_validity(&cert, "20260101000000", "20401231235959");
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_basic_constraints(&cert, 0, -1);
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_key_usage(&cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (ret != 0) {
        result = LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }

    ret = mbedtls_x509write_crt_pem(
        &cert,
        (unsigned char *)identity->cert_pem,
        sizeof(identity->cert_pem),
        tls_rng,
        &rng);
    if (ret != 0) {
        result = rng.failed ? LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED : LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
        goto cleanup;
    }
    identity->cert_pem_len = strlen(identity->cert_pem);

    result = compute_cert_fingerprint(identity);

cleanup:
    mbedtls_x509write_crt_free(&cert);
    mbedtls_pk_free(&key);
    if (result != LOCAL_TLS_IDENTITY_OK) {
        local_tls_identity_zeroize(identity);
    }
    return result;
}

void local_tls_identity_zeroize(local_tls_identity_t *identity)
{
    if (identity == NULL) {
        return;
    }
    secure_zero(identity, sizeof(*identity));
}

const char *local_tls_identity_result_name(local_tls_identity_result_t result)
{
    switch (result) {
        case LOCAL_TLS_IDENTITY_OK:
            return "ok";
        case LOCAL_TLS_IDENTITY_ERR_INVALID_ARG:
            return "invalid_arg";
        case LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED:
            return "random_failed";
        case LOCAL_TLS_IDENTITY_ERR_KEYGEN_FAILED:
            return "keygen_failed";
        case LOCAL_TLS_IDENTITY_ERR_CERT_FAILED:
            return "cert_failed";
        case LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED:
            return "fingerprint_failed";
        default:
            return "unknown";
    }
}
