#include "web_security.h"

#include <string.h>

#include "web_request.h"

#define TOKEN_RANDOM_BYTES (WEB_SECURITY_TOKEN_LEN / 2)

static void wipe(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static bool make_token(char out[WEB_SECURITY_TOKEN_BUF_LEN], web_security_random_fn_t random_fn, void *random_ctx)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t bytes[TOKEN_RANDOM_BYTES];
    if (random_fn == NULL || !random_fn(bytes, sizeof(bytes), random_ctx)) {
        wipe(bytes, sizeof(bytes));
        return false;
    }
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[(i * 2) + 1] = hex[bytes[i] & 0x0f];
    }
    out[WEB_SECURITY_TOKEN_LEN] = '\0';
    wipe(bytes, sizeof(bytes));
    return true;
}

static bool token_well_formed(const char *token)
{
    return token != NULL && strnlen(token, WEB_SECURITY_TOKEN_BUF_LEN) == WEB_SECURITY_TOKEN_LEN;
}

static bool live_session_matches(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return state != NULL && state->active && now_ms <= state->expires_ms && token_well_formed(token) &&
           web_request_secret_equal(state->session_token, token, WEB_SECURITY_TOKEN_LEN);
}

void web_security_init(web_security_state_t *state)
{
    if (state != NULL) {
        wipe(state, sizeof(*state));
    }
}

bool web_security_start_session(web_security_state_t *state, uint64_t now_ms, web_security_random_fn_t random_fn, void *random_ctx)
{
    if (state == NULL) {
        return false;
    }
    web_security_state_t next = {0};
    if (!make_token(next.session_token, random_fn, random_ctx) || !make_token(next.csrf_token, random_fn, random_ctx)) {
        wipe(&next, sizeof(next));
        return false;
    }
    next.expires_ms = now_ms + WEB_SECURITY_SESSION_IDLE_MS;
    next.active = true;
    *state = next;
    wipe(&next, sizeof(next));
    return true;
}

bool web_security_session_valid(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return live_session_matches(state, token, now_ms);
}

bool web_security_touch(web_security_state_t *state, const char *token, uint64_t now_ms)
{
    if (!live_session_matches(state, token, now_ms)) {
        return false;
    }
    state->expires_ms = now_ms + WEB_SECURITY_SESSION_IDLE_MS;
    return true;
}

bool web_security_csrf_valid(const web_security_state_t *state, const char *session_token, const char *csrf_token, uint64_t now_ms)
{
    return live_session_matches(state, session_token, now_ms) && token_well_formed(csrf_token) &&
           web_request_secret_equal(state->csrf_token, csrf_token, WEB_SECURITY_TOKEN_LEN);
}

const char *web_security_csrf_for_session(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return live_session_matches(state, token, now_ms) ? state->csrf_token : NULL;
}

bool web_security_has_session(const web_security_state_t *state, uint64_t now_ms)
{
    return state != NULL && state->active && now_ms <= state->expires_ms;
}

void web_security_end_session(web_security_state_t *state)
{
    if (state != NULL) {
        wipe(state, sizeof(*state));
    }
}
