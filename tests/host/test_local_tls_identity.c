#include "local_tls_identity.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool deterministic_random(uint8_t *buf, size_t len, void *ctx)
{
    uint8_t *state = (uint8_t *)ctx;
    if (buf == NULL || state == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        *state = (uint8_t)(*state * 33U + 17U);
        buf[i] = *state;
    }
    return true;
}

static void generates_self_signed_identity_with_display_fingerprint(void)
{
    uint8_t rng_state = 7;
    local_tls_identity_t identity;
    assert(local_tls_identity_generate(&identity, deterministic_random, &rng_state, "kvm.local") == LOCAL_TLS_IDENTITY_OK);

    assert(identity.cert_pem_len > 0);
    assert(identity.key_pem_len > 0);
    assert(strstr(identity.cert_pem, "BEGIN CERTIFICATE") != NULL);
    assert(strstr(identity.key_pem, "BEGIN") != NULL);
    assert(strlen(identity.fingerprint_text) == HTTPS_FINGERPRINT_TEXT_LEN - 1U);
    assert(identity.fingerprint_text[2] == ':');
    assert(identity.fingerprint_text[95] == '\0');
}

static void rejects_invalid_inputs_fail_closed(void)
{
    uint8_t rng_state = 1;
    local_tls_identity_t identity;
    assert(local_tls_identity_generate(NULL, deterministic_random, &rng_state, "kvm.local") == LOCAL_TLS_IDENTITY_ERR_INVALID_ARG);
    assert(local_tls_identity_generate(&identity, NULL, &rng_state, "kvm.local") == LOCAL_TLS_IDENTITY_ERR_INVALID_ARG);
    assert(local_tls_identity_generate(&identity, deterministic_random, NULL, "kvm.local") == LOCAL_TLS_IDENTITY_ERR_RANDOM_FAILED);
    assert(local_tls_identity_generate(&identity, deterministic_random, &rng_state, NULL) == LOCAL_TLS_IDENTITY_ERR_INVALID_ARG);
}

static void zeroize_clears_sensitive_buffers(void)
{
    uint8_t rng_state = 3;
    local_tls_identity_t identity;
    assert(local_tls_identity_generate(&identity, deterministic_random, &rng_state, "kvm.local") == LOCAL_TLS_IDENTITY_OK);
    local_tls_identity_zeroize(&identity);
    assert(identity.cert_pem_len == 0);
    assert(identity.key_pem_len == 0);
    assert(identity.fingerprint_text[0] == '\0');
    for (size_t i = 0; i < sizeof(identity.key_pem); ++i) {
        assert(identity.key_pem[i] == '\0');
    }
}

int main(void)
{
    generates_self_signed_identity_with_display_fingerprint();
    rejects_invalid_inputs_fail_closed();
    zeroize_clears_sensitive_buffers();
    return 0;
}
