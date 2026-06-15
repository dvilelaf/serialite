#include "web_security.h"

#include "credentials.h"

#include <string.h>

#define SESSION_TTL_MS (15ULL * 60ULL * 1000ULL)
#define LOCKOUT_AFTER_FAILURES 3
#define LOCKOUT_MS (60ULL * 1000ULL)
#define MIN_PASSWORD_LEN 6
#define TOKEN_RANDOM_BYTES 16

static bool bounded_strlen(const char *value, size_t max_len, size_t *out_len)
{
    if (value == NULL) {
        return false;
    }

    size_t len = 0;
    while (len <= max_len && value[len] != '\0') {
        len++;
    }
    if (len > max_len) {
        return false;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return true;
}

static bool password_for_hash_setup(const char *input, char out[WEB_SECURITY_PASSWORD_MAX_LEN])
{
    if (input == NULL || out == NULL) {
        return false;
    }
    if (credentials_compact_human_phrase(input, CREDENTIALS_WEB_PASSWORD_WORD_COUNT, out, WEB_SECURITY_PASSWORD_MAX_LEN)) {
        return true;
    }

    size_t len = 0;
    if (!bounded_strlen(input, WEB_SECURITY_PASSWORD_MAX_LEN - 1, &len) || len == 0) {
        return false;
    }
    memcpy(out, input, len + 1U);
    return true;
}

static void token_from_bytes(const uint8_t *bytes, char out[WEB_SECURITY_TOKEN_BUF_LEN])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < TOKEN_RANDOM_BYTES; ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[(i * 2) + 1] = hex[bytes[i] & 0x0f];
    }
    out[WEB_SECURITY_TOKEN_LEN] = '\0';
}

static bool make_token(char out[WEB_SECURITY_TOKEN_BUF_LEN], web_security_random_fn_t random_fn, void *random_ctx)
{
    if (random_fn == NULL) {
        return false;
    }

    uint8_t bytes[TOKEN_RANDOM_BYTES];
    if (!random_fn(bytes, sizeof(bytes), random_ctx)) {
        memset(bytes, 0, sizeof(bytes));
        return false;
    }
    token_from_bytes(bytes, out);
    memset(bytes, 0, sizeof(bytes));
    return true;
}

static web_security_session_t *find_session(web_security_state_t *state, const char *token, uint64_t now_ms)
{
    if (state == NULL || token == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < WEB_SECURITY_MAX_SESSIONS; ++i) {
        web_security_session_t *session = &state->sessions[i];
        if (session->active && now_ms <= session->expires_ms &&
            strncmp(session->session_token, token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
            return session;
        }
    }
    return NULL;
}

static web_security_session_t *find_session_by_token(web_security_state_t *state, const char *token)
{
    if (state == NULL || token == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < WEB_SECURITY_MAX_SESSIONS; ++i) {
        web_security_session_t *session = &state->sessions[i];
        if (session->active && strncmp(session->session_token, token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
            return session;
        }
    }
    return NULL;
}

static const web_security_session_t *find_session_const(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return find_session((web_security_state_t *)state, token, now_ms);
}

static web_security_session_t *oldest_or_free_session(web_security_state_t *state)
{
    web_security_session_t *slot = &state->sessions[0];
    for (size_t i = 0; i < WEB_SECURITY_MAX_SESSIONS; ++i) {
        if (!state->sessions[i].active) {
            return &state->sessions[i];
        }
        if (state->sessions[i].expires_ms < slot->expires_ms) {
            slot = &state->sessions[i];
        }
    }
    return slot;
}

static void clear_writer_if_session_inactive(web_security_state_t *state, uint64_t now_ms)
{
    if (state == NULL || state->writer_token[0] == '\0') {
        return;
    }

    if (find_session(state, state->writer_token, now_ms) == NULL) {
        memset(state->writer_token, 0, sizeof(state->writer_token));
    }
}

bool web_security_init(
    web_security_state_t *state,
    const char *password,
    web_security_random_fn_t random_fn,
    void *random_ctx)
{
    char hash_password[WEB_SECURITY_PASSWORD_MAX_LEN];
    size_t len = 0;
    if (state == NULL ||
        !password_for_hash_setup(password, hash_password) ||
        !bounded_strlen(hash_password, WEB_SECURITY_PASSWORD_MAX_LEN - 1, &len) ||
        len < MIN_PASSWORD_LEN) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    if (random_fn == NULL ||
        !random_fn(state->password_salt, sizeof(state->password_salt), random_ctx) ||
        !web_password_hash_derive(hash_password, state->password_salt, state->password_hash)) {
        memset(state, 0, sizeof(*state));
        memset(hash_password, 0, sizeof(hash_password));
        return false;
    }
    memset(hash_password, 0, sizeof(hash_password));
    state->password_hash_configured = true;
    return true;
}

bool web_security_rotate_password(
    web_security_state_t *state,
    const char *new_password,
    web_security_random_fn_t random_fn,
    void *random_ctx)
{
    if (state == NULL) {
        return false;
    }
    uint8_t new_salt[WEB_PASSWORD_SALT_LEN];
    uint8_t new_hash[WEB_PASSWORD_HASH_LEN];
    if (!web_security_prepare_password_hash(new_password, random_fn, random_ctx, new_salt, new_hash)) {
        return false;
    }

    web_security_apply_password_hash(state, new_salt, new_hash);
    memset(new_salt, 0, sizeof(new_salt));
    memset(new_hash, 0, sizeof(new_hash));
    return true;
}

bool web_security_prepare_password_hash(
    const char *password,
    web_security_random_fn_t random_fn,
    void *random_ctx,
    uint8_t out_salt[WEB_PASSWORD_SALT_LEN],
    uint8_t out_hash[WEB_PASSWORD_HASH_LEN])
{
    char hash_password[WEB_SECURITY_PASSWORD_MAX_LEN];
    size_t len = 0;
    if (!password_for_hash_setup(password, hash_password) ||
        !bounded_strlen(hash_password, WEB_SECURITY_PASSWORD_MAX_LEN - 1, &len) ||
        len < MIN_PASSWORD_LEN || random_fn == NULL || out_salt == NULL || out_hash == NULL) {
        return false;
    }
    if (!random_fn(out_salt, WEB_PASSWORD_SALT_LEN, random_ctx) ||
        !web_password_hash_derive(hash_password, out_salt, out_hash)) {
        memset(out_salt, 0, WEB_PASSWORD_SALT_LEN);
        memset(out_hash, 0, WEB_PASSWORD_HASH_LEN);
        memset(hash_password, 0, sizeof(hash_password));
        return false;
    }
    memset(hash_password, 0, sizeof(hash_password));
    return true;
}

bool web_security_init_from_hash(
    web_security_state_t *state,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    const uint8_t hash[WEB_PASSWORD_HASH_LEN])
{
    if (state == NULL || salt == NULL || hash == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    memcpy(state->password_salt, salt, sizeof(state->password_salt));
    memcpy(state->password_hash, hash, sizeof(state->password_hash));
    state->password_hash_configured = true;
    return true;
}

void web_security_apply_password_hash(
    web_security_state_t *state,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    const uint8_t hash[WEB_PASSWORD_HASH_LEN])
{
    if (state == NULL || salt == NULL || hash == NULL) {
        return;
    }
    web_security_invalidate_all(state);
    memcpy(state->password_salt, salt, sizeof(state->password_salt));
    memcpy(state->password_hash, hash, sizeof(state->password_hash));
    state->lockout_until_ms = 0;
    state->failed_logins = 0;
    state->password_hash_configured = true;
}

web_security_login_result_t web_security_login(
    web_security_state_t *state,
    const char *password,
    uint64_t now_ms,
    web_security_random_fn_t random_fn,
    void *random_ctx)
{
    if (state == NULL || password == NULL) {
        return WEB_SECURITY_LOGIN_DENIED;
    }
    if (now_ms < state->lockout_until_ms) {
        return WEB_SECURITY_LOGIN_LOCKED;
    }
    uint8_t candidate_hash[WEB_PASSWORD_HASH_LEN];
    size_t password_len = 0;
    const bool password_input_ok = bounded_strlen(password, WEB_SECURITY_PASSWORD_MAX_LEN - 1, &password_len) &&
                                   password_len >= MIN_PASSWORD_LEN;
    bool password_ok = state->password_hash_configured &&
                       password_input_ok &&
                       web_password_hash_derive(password, state->password_salt, candidate_hash) &&
                       web_password_hash_equal(state->password_hash, candidate_hash);
    memset(candidate_hash, 0, sizeof(candidate_hash));
    if (!password_ok) {
        if (state->failed_logins < UINT8_MAX) {
            state->failed_logins++;
        }
        if (state->failed_logins >= LOCKOUT_AFTER_FAILURES) {
            state->lockout_until_ms = now_ms + LOCKOUT_MS;
        }
        return WEB_SECURITY_LOGIN_DENIED;
    }

    web_security_session_t *session = oldest_or_free_session(state);
    char new_session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char new_csrf_token[WEB_SECURITY_TOKEN_BUF_LEN];
    if (!make_token(new_session_token, random_fn, random_ctx) ||
        !make_token(new_csrf_token, random_fn, random_ctx)) {
        return WEB_SECURITY_LOGIN_DENIED;
    }

    if (strncmp(state->writer_token, session->session_token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
        memset(state->writer_token, 0, sizeof(state->writer_token));
    }
    memset(session, 0, sizeof(*session));
    memcpy(session->session_token, new_session_token, sizeof(new_session_token));
    memcpy(session->csrf_token, new_csrf_token, sizeof(new_csrf_token));
    session->expires_ms = now_ms + SESSION_TTL_MS;
    session->active = true;

    memcpy(state->session_token, new_session_token, sizeof(new_session_token));
    memcpy(state->csrf_token, new_csrf_token, sizeof(new_csrf_token));
    state->session_expires_ms = session->expires_ms;
    state->session_active = true;
    state->failed_logins = 0;
    state->lockout_until_ms = 0;
    return WEB_SECURITY_LOGIN_OK;
}

bool web_security_session_valid(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return find_session_const(state, token, now_ms) != NULL;
}

bool web_security_csrf_valid(const web_security_state_t *state, const char *session_token, const char *csrf_token, uint64_t now_ms)
{
    const web_security_session_t *session = find_session_const(state, session_token, now_ms);
    return session != NULL && csrf_token != NULL &&
           strncmp(session->csrf_token, csrf_token, WEB_SECURITY_TOKEN_BUF_LEN) == 0;
}

const char *web_security_csrf_for_session(const web_security_state_t *state, const char *session_token, uint64_t now_ms)
{
    const web_security_session_t *session = find_session_const(state, session_token, now_ms);
    return session != NULL ? session->csrf_token : NULL;
}

void web_security_logout(web_security_state_t *state, const char *token)
{
    web_security_session_t *session = find_session_by_token(state, token);
    if (session != NULL) {
        if (strncmp(state->writer_token, session->session_token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
            memset(state->writer_token, 0, sizeof(state->writer_token));
        }
        memset(session, 0, sizeof(*session));
    }
}

void web_security_invalidate_all(web_security_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state->session_token, 0, sizeof(state->session_token));
    memset(state->csrf_token, 0, sizeof(state->csrf_token));
    memset(state->writer_token, 0, sizeof(state->writer_token));
    memset(state->sessions, 0, sizeof(state->sessions));
    state->session_expires_ms = 0;
    state->session_active = false;
}

bool web_security_acquire_writer(web_security_state_t *state, const char *token, uint64_t now_ms)
{
    if (state == NULL || find_session(state, token, now_ms) == NULL) {
        return false;
    }
    clear_writer_if_session_inactive(state, now_ms);
    if (state->writer_token[0] != '\0' && strncmp(state->writer_token, token, WEB_SECURITY_TOKEN_BUF_LEN) != 0) {
        return false;
    }
    strncpy(state->writer_token, token, sizeof(state->writer_token) - 1);
    return true;
}

void web_security_release_writer(web_security_state_t *state, const char *token)
{
    if (state != NULL && token != NULL &&
        strncmp(state->writer_token, token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
        memset(state->writer_token, 0, sizeof(state->writer_token));
    }
}

bool web_security_can_write(const web_security_state_t *state, const char *token, uint64_t now_ms)
{
    return state != NULL && token != NULL &&
           find_session_const(state, token, now_ms) != NULL &&
           strncmp(state->writer_token, token, WEB_SECURITY_TOKEN_BUF_LEN) == 0;
}

web_security_writer_state_t web_security_writer_state(web_security_state_t *state, const char *token, uint64_t now_ms)
{
    if (state == NULL || token == NULL || find_session(state, token, now_ms) == NULL) {
        return WEB_SECURITY_WRITER_INVALID_SESSION;
    }

    clear_writer_if_session_inactive(state, now_ms);
    if (state->writer_token[0] == '\0') {
        return WEB_SECURITY_WRITER_READ_ONLY;
    }
    if (strncmp(state->writer_token, token, WEB_SECURITY_TOKEN_BUF_LEN) == 0) {
        return WEB_SECURITY_WRITER_ACTIVE;
    }
    return WEB_SECURITY_WRITER_BUSY;
}

bool web_security_origin_allowed(const char *origin, const char *host)
{
    if (origin == NULL || host == NULL || host[0] == '\0') {
        return false;
    }

    const char *prefix = "http://";
    const size_t prefix_len = strlen(prefix);
    if (strncmp(origin, prefix, prefix_len) != 0) {
        return false;
    }

    const char *origin_host = origin + prefix_len;
    size_t host_len = strlen(host);
    if (host_len > 3 && strcmp(host + host_len - 3, ":80") == 0) {
        host_len -= 3;
    }
    if (strncmp(origin_host, host, host_len) != 0) {
        return false;
    }

    const char next = origin_host[host_len];
    return next == '\0' || (next == ':' && strcmp(origin_host + host_len, ":80") == 0);
}
