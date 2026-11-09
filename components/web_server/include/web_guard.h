#pragma once

/*
 * The one place that decides whether a request reaches its handler. The
 * server gathers every fact before sending anything, asks this function, and
 * on rejection replies and returns without running the handler.
 */

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    /* Static pages and assets. */
    WEB_ACCESS_PUBLIC = 0,
    /* Same-origin POST that does not need a session yet (creating one). */
    WEB_ACCESS_ORIGIN,
    /* Reads that need the operator session cookie. */
    WEB_ACCESS_SESSION,
    /* WebSocket upgrade: session cookie and same Origin (browsers send no CSRF header here). */
    WEB_ACCESS_SESSION_ORIGIN,
    /* State changes: session cookie, same Origin, and the session's CSRF token. */
    WEB_ACCESS_SESSION_CSRF,
} web_guard_access_t;

typedef struct {
    web_guard_access_t access;
    size_t max_body_len;
    /* Refused while the emergency lock is engaged. */
    bool blocked_when_locked;
} web_guard_rule_t;

typedef struct {
    bool rate_limited;
    bool host_allowed;
    bool origin_allowed;
    bool session_valid;
    bool csrf_valid;
    bool locked;
    size_t body_len;
} web_guard_facts_t;

typedef enum {
    WEB_GUARD_ALLOW = 0,
    WEB_GUARD_REJECT_RATE_LIMIT,
    WEB_GUARD_REJECT_HOST,
    WEB_GUARD_REJECT_BODY_TOO_LARGE,
    WEB_GUARD_REJECT_LOCKED,
    WEB_GUARD_REJECT_ORIGIN,
    WEB_GUARD_REJECT_SESSION,
    WEB_GUARD_REJECT_CSRF,
} web_guard_result_t;

web_guard_result_t web_guard_evaluate(const web_guard_rule_t *rule, const web_guard_facts_t *facts);
/* HTTP status line for a rejection, e.g. "403 Forbidden". */
const char *web_guard_status(web_guard_result_t result);
const char *web_guard_result_name(web_guard_result_t result);
