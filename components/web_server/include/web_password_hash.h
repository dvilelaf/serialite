#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_PASSWORD_SALT_LEN 16
#define WEB_PASSWORD_HASH_LEN 32
#define WEB_PASSWORD_PBKDF2_ITERATIONS 20000

bool web_password_hash_derive(
    const char *password,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    uint8_t out_hash[WEB_PASSWORD_HASH_LEN]);
bool web_password_hash_equal(const uint8_t a[WEB_PASSWORD_HASH_LEN], const uint8_t b[WEB_PASSWORD_HASH_LEN]);
