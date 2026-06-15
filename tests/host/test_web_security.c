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

static void test_two_word_web_password_is_supported(void)
{
    web_security_state_t state;
    uint8_t counter = 44;

    CHECK(web_security_init(&state, "aim act", deterministic_random, &counter));
    CHECK(web_security_login(&state, "aimact", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
}

static void test_human_password_login_rejects_spaces_and_case_variants(void)
{
    web_security_state_t state;
    uint8_t counter = 44;

    CHECK(web_security_init(&state, "aim act", deterministic_random, &counter));
    CHECK(web_security_login(&state, "aim act", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, "AimAct", 1100, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
    CHECK(web_security_login(&state, " aimact", 1200, deterministic_random, &counter) == WEB_SECURITY_LOGIN_DENIED);
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

static void test_login_replaces_previous_session(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char first_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(first_token, state.session_token);
    CHECK(web_security_can_write(&state, first_token, 1000));

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char second_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(second_token, state.session_token);

    CHECK(!web_security_session_valid(&state, first_token, 2000));
    CHECK(!web_security_can_write(&state, first_token, 2000));
    CHECK(web_security_session_valid(&state, second_token, 2000));
    CHECK(web_security_can_write(&state, second_token, 2000));
}

static void test_writer_state_is_active_for_the_only_valid_session(void)
{
    web_security_state_t state = valid_state();
    uint8_t counter = 1;

    CHECK(web_security_writer_state(&state, "not-a-session", 1000) == WEB_SECURITY_WRITER_INVALID_SESSION);

    CHECK(web_security_login(&state, "correct horse battery staple", 1000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char first_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(first_token, state.session_token);
    CHECK(web_security_writer_state(&state, first_token, 1000) == WEB_SECURITY_WRITER_ACTIVE);

    CHECK(web_security_login(&state, "correct horse battery staple", 2000, deterministic_random, &counter) == WEB_SECURITY_LOGIN_OK);
    char second_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(second_token, state.session_token);
    CHECK(web_security_writer_state(&state, first_token, 2000) == WEB_SECURITY_WRITER_INVALID_SESSION);
    CHECK(web_security_writer_state(&state, second_token, 2000) == WEB_SECURITY_WRITER_ACTIVE);
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
    CHECK(web_security_can_write(&state, token, 1000));

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
    test_two_word_web_password_is_supported();
    test_human_password_login_rejects_spaces_and_case_variants();
    test_login_creates_session_and_csrf_tokens();
    test_wrong_password_rate_limits_login();
    test_session_expires_and_logout_invalidates_tokens();
    test_login_replaces_previous_session();
    test_writer_state_is_active_for_the_only_valid_session();
    test_origin_must_match_host();
    test_invalidate_all_clears_sessions_and_writer();
    test_rotate_password_invalidates_existing_sessions_and_accepts_new_password();
    test_init_from_persisted_hash_accepts_same_password();
    return 0;
}
