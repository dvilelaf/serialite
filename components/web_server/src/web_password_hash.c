#include "web_password_hash.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "mbedtls/pkcs5.h"
#else
#include <openssl/evp.h>
#endif

bool web_password_hash_derive(
    const char *password,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    uint8_t out_hash[WEB_PASSWORD_HASH_LEN])
{
    if (password == NULL || salt == NULL || out_hash == NULL) {
        return false;
    }

#ifdef ESP_PLATFORM
    const int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char *)password,
        strlen(password),
        salt,
        WEB_PASSWORD_SALT_LEN,
        WEB_PASSWORD_PBKDF2_ITERATIONS,
        WEB_PASSWORD_HASH_LEN,
        out_hash);
    return ret == 0;
#else
    return PKCS5_PBKDF2_HMAC(
               password,
               (int)strlen(password),
               salt,
               WEB_PASSWORD_SALT_LEN,
               WEB_PASSWORD_PBKDF2_ITERATIONS,
               EVP_sha256(),
               WEB_PASSWORD_HASH_LEN,
               out_hash) == 1;
#endif
}

bool web_password_hash_equal(const uint8_t a[WEB_PASSWORD_HASH_LEN], const uint8_t b[WEB_PASSWORD_HASH_LEN])
{
    if (a == NULL || b == NULL) {
        return false;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < WEB_PASSWORD_HASH_LEN; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}
