#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "local_pairing.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static bool deterministic_random(uint8_t *buf, size_t len, void *ctx)
{
    uint8_t *counter = (uint8_t *)ctx;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (*counter)++;
    }
    return true;
}

static void test_generates_six_digit_code(void)
{
    char code[LOCAL_PAIRING_CODE_BUF_LEN];
    uint8_t counter = 0;

    CHECK(local_pairing_generate_code(code, deterministic_random, &counter) == LOCAL_PAIRING_OK);
    CHECK(strlen(code) == LOCAL_PAIRING_CODE_LEN);
    CHECK(local_pairing_code_format_valid(code));
}

static void test_rejects_non_digit_or_wrong_length_codes(void)
{
    CHECK(local_pairing_code_format_valid("123456"));
    CHECK(!local_pairing_code_format_valid(""));
    CHECK(!local_pairing_code_format_valid("12345"));
    CHECK(!local_pairing_code_format_valid("1234567"));
    CHECK(!local_pairing_code_format_valid("12345x"));
    CHECK(!local_pairing_code_format_valid(NULL));
}

static void test_pairing_is_consumed_once_after_success(void)
{
    local_pairing_state_t state;
    CHECK(local_pairing_init(&state, "123456"));

    CHECK(local_pairing_required(&state));
    CHECK(!local_pairing_verify_and_consume(&state, "000000"));
    CHECK(local_pairing_required(&state));
    CHECK(local_pairing_verify_and_consume(&state, "123456"));
    CHECK(!local_pairing_required(&state));
    CHECK(local_pairing_verify_and_consume(&state, "000000"));
}

static void test_pairing_locks_after_repeated_wrong_codes(void)
{
    local_pairing_state_t state;
    CHECK(local_pairing_init(&state, "123456"));

    for (size_t i = 0; i < LOCAL_PAIRING_MAX_ATTEMPTS; ++i) {
        CHECK(!local_pairing_verify_and_consume(&state, "000000"));
    }

    CHECK(local_pairing_required(&state));
    CHECK(local_pairing_locked(&state));
    CHECK(!local_pairing_verify_and_consume(&state, "123456"));
}

static void test_invalid_init_fails_closed(void)
{
    local_pairing_state_t state;

    CHECK(!local_pairing_init(&state, NULL));
    CHECK(local_pairing_required(&state));
    CHECK(local_pairing_locked(&state));
    CHECK(!local_pairing_verify_and_consume(&state, "123456"));

    CHECK(!local_pairing_init(&state, "12345"));
    CHECK(local_pairing_required(&state));
    CHECK(local_pairing_locked(&state));
    CHECK(!local_pairing_verify_and_consume(&state, "123456"));
}

int main(void)
{
    test_generates_six_digit_code();
    test_rejects_non_digit_or_wrong_length_codes();
    test_pairing_is_consumed_once_after_success();
    test_pairing_locks_after_repeated_wrong_codes();
    test_invalid_init_fails_closed();
    return 0;
}
