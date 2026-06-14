#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_security.h"

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

static web_security_state_t valid_state(void)
{
    web_security_state_t state;
    uint8_t counter = 200;
    CHECK(web_security_init(&state, "correct horse battery staple", deterministic_random, &counter));
    return state;
}

static bool memory_contains(const void *haystack, size_t haystack_len, const char *needle)
{
    const size_t needle_len = strlen(needle);
    const unsigned char *bytes = (const unsigned char *)haystack;
    for (size_t i = 0; i + needle_len <= haystack_len; ++i) {
        if (memcmp(bytes + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static void test_password_is_not_stored_in_plaintext(void)
{
    web_security_state_t state = valid_state();

    CHECK(!memory_contains(&state, sizeof(state), "correct horse battery staple"));
    CHECK(state.password_hash_configured);
}

static void test_eight_character_password_remains_supported(void)
{
    web_security_state_t state;
    uint8_t counter = 44;

    CHECK(web_security_init(&state, "eight888", deterministic_random, &counter));
    CHECK(web_security_login(&state, "eight888", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
}

static void test_login_creates_session_and_csrf_tokens(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    CHECK(web_security_session_valid(&state, state.session_token, 1000));
    CHECK(web_security_csrf_valid(&state, state.session_token, state.csrf_token, 1000));
    CHECK(strlen(state.session_token) == WEB_SECURITY_TOKEN_LEN);
    CHECK(strlen(state.csrf_token) == WEB_SECURITY_TOKEN_LEN);
}

static void test_wrong_password_rate_limits_login(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "wrong", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, "wrong", 1100, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, "wrong", 1200, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, "correct horse battery staple", 1300, deterministic_random, &counter) == WEB_SECURITY_LOGIN_LOCKED);
    CHECK(web_security_login(&state, "correct horse battery staple", 61300, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
}

static void test_session_expires_and_logout_invalidates_tokens(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(token, state.session_token);

    CHECK(web_security_session_valid(&state, token, 1000));
    CHECK(!web_security_session_valid(&state, token, 16 * 60 * 1000));

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    strcpy(token, state.session_token);
    web_security_logout(&state, token);
    CHECK(!web_security_session_valid(&state, token, 2000));
}

static void test_single_writer_lock(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char first_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(first_token, state.session_token);
    CHECK(web_security_acquire_writer(&state, first_token, 1000));
    CHECK(web_security_can_write(&state, first_token, 1000));

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char second_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(second_token, state.session_token);
    CHECK(!web_security_acquire_writer(&state, second_token, 2000));
    CHECK(!web_security_can_write(&state, second_token, 2000));

    web_security_release_writer(&state, first_token);
    CHECK(web_security_acquire_writer(&state, second_token, 2000));
    CHECK(web_security_can_write(&state, second_token, 2000));
}

static void test_writer_state_reports_read_only_active_busy_and_invalid(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_writer_state(&state, "not-a-session", 1000) == WEB_SECURITY_WRITER_INVALID_SESSION);

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char first_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(first_token, state.session_token);
    CHECK(web_security_writer_state(&state, first_token, 1000) == WEB_SECURITY_WRITER_READ_ONLY);
    CHECK(web_security_acquire_writer(&state, first_token, 1000));
    CHECK(web_security_writer_state(&state, first_token, 1000) == WEB_SECURITY_WRITER_ACTIVE);

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char second_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(second_token, state.session_token);
    CHECK(web_security_writer_state(&state, second_token, 2000) == WEB_SECURITY_WRITER_BUSY);

    web_security_release_writer(&state, first_token);
    CHECK(web_security_writer_state(&state, second_token, 2000) == WEB_SECURITY_WRITER_READ_ONLY);
}

static void test_expired_writer_lock_does_not_block_new_writer(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char first_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(first_token, state.session_token);
    CHECK(web_security_acquire_writer(&state, first_token, 1000));

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char second_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(second_token, state.session_token);

    const uint64_t after_first_expires = 901500;
    CHECK(!web_security_can_write(&state, first_token, after_first_expires));
    CHECK(web_security_acquire_writer(&state, second_token, after_first_expires));
    CHECK(web_security_can_write(&state, second_token, after_first_expires));
}

static void test_evicted_writer_lock_does_not_block_new_writer(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char writer_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(writer_token, state.session_token);
    CHECK(web_security_acquire_writer(&state, writer_token, 1000));

    char latest_token[WEB_SECURITY_TOKEN_BUF_LEN];
    for (size_t i = 0; i < WEB_SECURITY_MAX_SESSIONS; ++i) {
        CHECK(web_security_login(&state, "correct horse battery staple", 2000 + i, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
        strcpy(latest_token, state.session_token);
    }

    CHECK(!web_security_session_valid(&state, writer_token, 3000));
    CHECK(web_security_acquire_writer(&state, latest_token, 3000));
    CHECK(web_security_can_write(&state, latest_token, 3000));
}

static void test_origin_must_match_host(void)
{
    CHECK(web_security_origin_allowed("http://192.168.4.1", "192.168.4.1"));
    CHECK(web_security_origin_allowed("http://192.168.4.1:80", "192.168.4.1"));
    CHECK(web_security_origin_allowed("http://192.168.4.1", "192.168.4.1:80"));
    CHECK(!web_security_origin_allowed("http://evil.local", "192.168.4.1"));
    CHECK(!web_security_origin_allowed(NULL, "192.168.4.1"));
    CHECK(!web_security_origin_allowed("http://192.168.4.1", NULL));
}

static void test_invalidate_all_clears_sessions_and_writer(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(token, state.session_token);
    CHECK(web_security_acquire_writer(&state, token, 1000));

    web_security_invalidate_all(&state);

    CHECK(!web_security_session_valid(&state, token, 1000));
    CHECK(!web_security_can_write(&state, token, 1000));
    CHECK(state.writer_token[0] == '\0');
}

static void test_rotate_password_invalidates_existing_sessions_and_accepts_new_password(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char old_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(old_token, state.session_token);

    CHECK(web_security_rotate_password(&state, "new horse battery staple", deterministic_random, &counter));

    CHECK(!web_security_session_valid(&state, old_token, 2000));
    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, "new horse battery staple", 3000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
}

static void test_init_from_persisted_hash_accepts_same_password(void)
{
    uint8_t counter = 1;
    uint8_t salt[WEB_PASSWORD_SALT_LEN];
    uint8_t hash[WEB_PASSWORD_HASH_LEN];
    CHECK(web_security_prepare_password_hash("persisted horse battery staple", deterministic_random, &counter, salt, hash));

    web_security_state_t state;
    CHECK(web_security_init_from_hash(&state, salt, hash));

    CHECK(web_security_login(&state, "persisted horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    CHECK(web_security_login(&state, "wrong horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
}

int main(void)
{
    test_password_is_not_stored_in_plaintext();
    test_eight_character_password_remains_supported();
    test_login_creates_session_and_csrf_tokens();
    test_wrong_password_rate_limits_login();
    test_session_expires_and_logout_invalidates_tokens();
    test_single_writer_lock();
    test_writer_state_reports_read_only_active_busy_and_invalid();
    test_expired_writer_lock_does_not_block_new_writer();
    test_evicted_writer_lock_does_not_block_new_writer();
    test_origin_must_match_host();
    test_invalidate_all_clears_sessions_and_writer();
    test_rotate_password_invalidates_existing_sessions_and_accepts_new_password();
    test_init_from_persisted_hash_accepts_same_password();
    return 0;
}
