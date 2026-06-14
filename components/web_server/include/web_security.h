#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_SECURITY_TOKEN_LEN 32
#define WEB_SECURITY_TOKEN_BUF_LEN (WEB_SECURITY_TOKEN_LEN + 1)
#define WEB_SECURITY_PASSWORD_MAX_LEN 64
#define WEB_SECURITY_MAX_SESSIONS 4

#include "web_password_hash.h"

typedef enum {
    WEB_SECURITY_LOGIN_OK = 0,
    WEB_SECURITY_LOGIN_DENIED,
    WEB_SECURITY_LOGIN_LOCKED,
} web_security_login_result_t;

typedef enum {
    WEB_SECURITY_WRITER_INVALID_SESSION = 0,
    WEB_SECURITY_WRITER_READ_ONLY,
    WEB_SECURITY_WRITER_ACTIVE,
    WEB_SECURITY_WRITER_BUSY,
} web_security_writer_state_t;

typedef bool (*web_security_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef struct {
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char csrf_token[WEB_SECURITY_TOKEN_BUF_LEN];
    uint64_t expires_ms;
    bool active;
} web_security_session_t;

typedef struct {
    uint8_t password_salt[WEB_PASSWORD_SALT_LEN];
    uint8_t password_hash[WEB_PASSWORD_HASH_LEN];
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char csrf_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char writer_token[WEB_SECURITY_TOKEN_BUF_LEN];
    web_security_session_t sessions[WEB_SECURITY_MAX_SESSIONS];
    uint64_t session_expires_ms;
    uint64_t lockout_until_ms;
    uint8_t failed_logins;
    bool password_hash_configured;
    bool session_active;
} web_security_state_t;

bool web_security_init(
    web_security_state_t *state,
    const char *password,
    web_security_random_fn_t random_fn,
    void *random_ctx);
bool web_security_rotate_password(
    web_security_state_t *state,
    const char *new_password,
    web_security_random_fn_t random_fn,
    void *random_ctx);
bool web_security_prepare_password_hash(
    const char *password,
    web_security_random_fn_t random_fn,
    void *random_ctx,
    uint8_t out_salt[WEB_PASSWORD_SALT_LEN],
    uint8_t out_hash[WEB_PASSWORD_HASH_LEN]);
bool web_security_init_from_hash(
    web_security_state_t *state,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    const uint8_t hash[WEB_PASSWORD_HASH_LEN]);
void web_security_apply_password_hash(
    web_security_state_t *state,
    const uint8_t salt[WEB_PASSWORD_SALT_LEN],
    const uint8_t hash[WEB_PASSWORD_HASH_LEN]);
web_security_login_result_t web_security_login(
    web_security_state_t *state,
    const char *password,
    uint64_t now_ms,
    web_security_random_fn_t random_fn,
    void *random_ctx);
bool web_security_session_valid(const web_security_state_t *state, const char *token, uint64_t now_ms);
bool web_security_csrf_valid(const web_security_state_t *state, const char *session_token, const char *csrf_token, uint64_t now_ms);
const char *web_security_csrf_for_session(const web_security_state_t *state, const char *session_token, uint64_t now_ms);
void web_security_logout(web_security_state_t *state, const char *token);
void web_security_invalidate_all(web_security_state_t *state);
bool web_security_acquire_writer(web_security_state_t *state, const char *token, uint64_t now_ms);
void web_security_release_writer(web_security_state_t *state, const char *token);
bool web_security_can_write(const web_security_state_t *state, const char *token, uint64_t now_ms);
web_security_writer_state_t web_security_writer_state(web_security_state_t *state, const char *token, uint64_t now_ms);
bool web_security_origin_allowed(const char *origin, const char *host);
