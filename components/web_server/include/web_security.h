#pragma once

/*
 * The single operator session. There is no web password: joining the AP
 * already required the WPA passphrase shown on the device, and a session can
 * only be created by a same-origin POST, so other sites cannot mint one.
 * Starting a session replaces the previous one.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_SECURITY_TOKEN_LEN 32
#define WEB_SECURITY_TOKEN_BUF_LEN (WEB_SECURITY_TOKEN_LEN + 1)
#define WEB_SECURITY_SESSION_IDLE_MS (15ULL * 60ULL * 1000ULL)

typedef bool (*web_security_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef struct {
    char session_token[WEB_SECURITY_TOKEN_BUF_LEN];
    char csrf_token[WEB_SECURITY_TOKEN_BUF_LEN];
    uint64_t expires_ms;
    bool active;
} web_security_state_t;

void web_security_init(web_security_state_t *state);
/* Replaces any existing session with a fresh one from 128-bit random tokens. */
bool web_security_start_session(web_security_state_t *state, uint64_t now_ms, web_security_random_fn_t random_fn, void *random_ctx);
bool web_security_session_valid(const web_security_state_t *state, const char *token, uint64_t now_ms);
/* Like session_valid, and pushes the idle timeout forward. */
bool web_security_touch(web_security_state_t *state, const char *token, uint64_t now_ms);
bool web_security_csrf_valid(const web_security_state_t *state, const char *session_token, const char *csrf_token, uint64_t now_ms);
/* NULL unless token names the live session. */
const char *web_security_csrf_for_session(const web_security_state_t *state, const char *token, uint64_t now_ms);
bool web_security_has_session(const web_security_state_t *state, uint64_t now_ms);
void web_security_end_session(web_security_state_t *state);
