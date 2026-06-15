#include "local_tls_identity.h"

#include <string.h>

local_tls_identity_result_t local_tls_identity_generate(
    local_tls_identity_t *identity,
    local_tls_identity_random_fn_t random_fn,
    void *random_ctx,
    const char *common_name)
{
    (void)random_fn;
    (void)random_ctx;
    (void)common_name;
    if (identity == NULL) {
        return LOCAL_TLS_IDENTITY_ERR_INVALID_ARG;
    }
    memset(identity, 0, sizeof(*identity));
    return LOCAL_TLS_IDENTITY_ERR_CERT_FAILED;
}

void local_tls_identity_zeroize(local_tls_identity_t *identity)
{
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
    }
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
        case LOCAL_TLS_IDENTITY_ERR_FINGERPRINT_FAILED:
            return "fingerprint_failed";
        case LOCAL_TLS_IDENTITY_ERR_CERT_FAILED:
        default:
            return "cert_failed";
    }
}
