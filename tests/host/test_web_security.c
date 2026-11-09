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

static bool counter_random(uint8_t *buf, size_t len, void *ctx)
{
    uint8_t *counter = (uint8_t *)ctx;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (*counter)++;
    }
    return true;
}

static bool failing_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)buf;
    (void)len;
    (void)ctx;
    return false;
}

static web_security_state_t started(uint8_t seed, uint64_t now)
{
    web_security_state_t state;
    web_security_init(&state);
    CHECK(web_security_start_session(&state, now, counter_random, &seed));
    return state;
}

static void test_new_session_has_distinct_hex_tokens(void)
{
    web_security_state_t state = started(0, 1000);
    CHECK(strlen(state.session_token) == WEB_SECURITY_TOKEN_LEN);
    CHECK(strlen(state.csrf_token) == WEB_SECURITY_TOKEN_LEN);
    CHECK(strcmp(state.session_token, state.csrf_token) != 0);
    CHECK(strspn(state.session_token, "0123456789abcdef") == WEB_SECURITY_TOKEN_LEN);
    CHECK(web_security_session_valid(&state, state.session_token, 1000));
    CHECK(web_security_has_session(&state, 1000));
}

static void test_session_and_csrf_checks(void)
{
    web_security_state_t state = started(10, 1000);
    char wrong[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(wrong, state.session_token);
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';

    CHECK(!web_security_session_valid(&state, wrong, 1000));
    CHECK(!web_security_session_valid(&state, NULL, 1000));
    CHECK(!web_security_session_valid(&state, "short", 1000));
    CHECK(web_security_csrf_valid(&state, state.session_token, state.csrf_token, 1000));
    CHECK(!web_security_csrf_valid(&state, state.session_token, state.session_token, 1000));
    CHECK(!web_security_csrf_valid(&state, wrong, state.csrf_token, 1000));
    CHECK(!web_security_csrf_valid(&state, state.session_token, NULL, 1000));
    CHECK(strcmp(web_security_csrf_for_session(&state, state.session_token, 1000), state.csrf_token) == 0);
    CHECK(web_security_csrf_for_session(&state, wrong, 1000) == NULL);
}

static void test_idle_timeout_slides_with_activity(void)
{
    web_security_state_t state = started(20, 0);
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(token, state.session_token);

    CHECK(web_security_session_valid(&state, token, WEB_SECURITY_SESSION_IDLE_MS));
    CHECK(web_security_touch(&state, token, WEB_SECURITY_SESSION_IDLE_MS));
    CHECK(web_security_session_valid(&state, token, 2 * WEB_SECURITY_SESSION_IDLE_MS));
    CHECK(!web_security_session_valid(&state, token, 2 * WEB_SECURITY_SESSION_IDLE_MS + 1));
    CHECK(!web_security_touch(&state, token, 2 * WEB_SECURITY_SESSION_IDLE_MS + 1));
    CHECK(!web_security_has_session(&state, 2 * WEB_SECURITY_SESSION_IDLE_MS + 1));
}

static void test_new_session_replaces_the_old_one(void)
{
    web_security_state_t state = started(30, 1000);
    char old_token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(old_token, state.session_token);

    uint8_t seed = 99;
    CHECK(web_security_start_session(&state, 2000, counter_random, &seed));
    CHECK(!web_security_session_valid(&state, old_token, 2000));
    CHECK(web_security_session_valid(&state, state.session_token, 2000));
}

static void test_end_session_wipes_tokens(void)
{
    web_security_state_t state = started(40, 1000);
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(token, state.session_token);

    web_security_end_session(&state);
    CHECK(!web_security_session_valid(&state, token, 1000));
    CHECK(!web_security_has_session(&state, 1000));
    const web_security_state_t zero = {0};
    CHECK(memcmp(&state, &zero, sizeof(state)) == 0);
}

static void test_random_failure_keeps_the_existing_session(void)
{
    web_security_state_t state = started(50, 1000);
    char token[WEB_SECURITY_TOKEN_BUF_LEN];
    strcpy(token, state.session_token);

    CHECK(!web_security_start_session(&state, 1000, failing_random, NULL));
    CHECK(web_security_session_valid(&state, token, 1000));
    CHECK(!web_security_start_session(&state, 1000, NULL, NULL));
}

int main(void)
{
    test_new_session_has_distinct_hex_tokens();
    test_session_and_csrf_checks();
    test_idle_timeout_slides_with_activity();
    test_new_session_replaces_the_old_one();
    test_end_session_wipes_tokens();
    test_random_failure_keeps_the_existing_session();
    return 0;
}
