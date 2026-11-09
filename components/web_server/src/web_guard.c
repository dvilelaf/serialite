#include "web_guard.h"

web_guard_result_t web_guard_evaluate(const web_guard_rule_t *rule, const web_guard_facts_t *facts)
{
    if (rule == NULL || facts == NULL) {
        return WEB_GUARD_REJECT_SESSION;
    }
    if (facts->rate_limited) {
        return WEB_GUARD_REJECT_RATE_LIMIT;
    }
    if (!facts->host_allowed) {
        return WEB_GUARD_REJECT_HOST;
    }
    if (facts->body_len > rule->max_body_len) {
        return WEB_GUARD_REJECT_BODY_TOO_LARGE;
    }
    if (rule->blocked_when_locked && facts->locked) {
        return WEB_GUARD_REJECT_LOCKED;
    }

    switch (rule->access) {
        case WEB_ACCESS_PUBLIC:
            return WEB_GUARD_ALLOW;
        case WEB_ACCESS_ORIGIN:
            return facts->origin_allowed ? WEB_GUARD_ALLOW : WEB_GUARD_REJECT_ORIGIN;
        case WEB_ACCESS_SESSION:
            return facts->session_valid ? WEB_GUARD_ALLOW : WEB_GUARD_REJECT_SESSION;
        case WEB_ACCESS_SESSION_ORIGIN:
            if (!facts->origin_allowed) {
                return WEB_GUARD_REJECT_ORIGIN;
            }
            return facts->session_valid ? WEB_GUARD_ALLOW : WEB_GUARD_REJECT_SESSION;
        case WEB_ACCESS_SESSION_CSRF:
            if (!facts->origin_allowed) {
                return WEB_GUARD_REJECT_ORIGIN;
            }
            if (!facts->session_valid) {
                return WEB_GUARD_REJECT_SESSION;
            }
            return facts->csrf_valid ? WEB_GUARD_ALLOW : WEB_GUARD_REJECT_CSRF;
        default:
            return WEB_GUARD_REJECT_SESSION;
    }
}

const char *web_guard_status(web_guard_result_t result)
{
    switch (result) {
        case WEB_GUARD_ALLOW:
            return "200 OK";
        case WEB_GUARD_REJECT_RATE_LIMIT:
            return "429 Too Many Requests";
        case WEB_GUARD_REJECT_HOST:
            return "421 Misdirected Request";
        case WEB_GUARD_REJECT_BODY_TOO_LARGE:
            return "413 Content Too Large";
        case WEB_GUARD_REJECT_LOCKED:
            return "423 Locked";
        case WEB_GUARD_REJECT_SESSION:
            return "401 Unauthorized";
        case WEB_GUARD_REJECT_ORIGIN:
        case WEB_GUARD_REJECT_CSRF:
        default:
            return "403 Forbidden";
    }
}

const char *web_guard_result_name(web_guard_result_t result)
{
    switch (result) {
        case WEB_GUARD_ALLOW:
            return "allow";
        case WEB_GUARD_REJECT_RATE_LIMIT:
            return "rate limit";
        case WEB_GUARD_REJECT_HOST:
            return "host not allowed";
        case WEB_GUARD_REJECT_BODY_TOO_LARGE:
            return "body too large";
        case WEB_GUARD_REJECT_LOCKED:
            return "emergency lock";
        case WEB_GUARD_REJECT_ORIGIN:
            return "origin not allowed";
        case WEB_GUARD_REJECT_SESSION:
            return "no session";
        case WEB_GUARD_REJECT_CSRF:
            return "csrf token";
        default:
            return "unknown";
    }
}
