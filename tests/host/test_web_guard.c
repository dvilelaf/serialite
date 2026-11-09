#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_guard.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static const web_guard_rule_t PUBLIC = {.access = WEB_ACCESS_PUBLIC};
static const web_guard_rule_t SESSION_CREATE = {.access = WEB_ACCESS_ORIGIN, .blocked_when_locked = true};
static const web_guard_rule_t READ = {.access = WEB_ACCESS_SESSION};
static const web_guard_rule_t WEBSOCKET = {.access = WEB_ACCESS_SESSION_ORIGIN, .blocked_when_locked = true};
static const web_guard_rule_t WRITE = {.access = WEB_ACCESS_SESSION_CSRF, .max_body_len = 512, .blocked_when_locked = true};

static web_guard_facts_t good(void)
{
    return (web_guard_facts_t){
        .host_allowed = true,
        .origin_allowed = true,
        .session_valid = true,
        .csrf_valid = true,
    };
}

static void test_everything_valid_is_allowed(void)
{
    const web_guard_facts_t facts = good();
    CHECK(web_guard_evaluate(&PUBLIC, &facts) == WEB_GUARD_ALLOW);
    CHECK(web_guard_evaluate(&SESSION_CREATE, &facts) == WEB_GUARD_ALLOW);
    CHECK(web_guard_evaluate(&READ, &facts) == WEB_GUARD_ALLOW);
    CHECK(web_guard_evaluate(&WEBSOCKET, &facts) == WEB_GUARD_ALLOW);
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_ALLOW);
}

static void test_rate_limit_and_host_apply_to_every_route(void)
{
    web_guard_facts_t facts = good();
    facts.rate_limited = true;
    CHECK(web_guard_evaluate(&PUBLIC, &facts) == WEB_GUARD_REJECT_RATE_LIMIT);

    facts = good();
    facts.host_allowed = false;
    CHECK(web_guard_evaluate(&PUBLIC, &facts) == WEB_GUARD_REJECT_HOST);
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_HOST);
}

static void test_mutations_need_origin_session_and_csrf(void)
{
    web_guard_facts_t facts = good();
    facts.origin_allowed = false;
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_ORIGIN);
    CHECK(web_guard_evaluate(&SESSION_CREATE, &facts) == WEB_GUARD_REJECT_ORIGIN);
    CHECK(web_guard_evaluate(&WEBSOCKET, &facts) == WEB_GUARD_REJECT_ORIGIN);
    CHECK(web_guard_evaluate(&READ, &facts) == WEB_GUARD_ALLOW);

    facts = good();
    facts.session_valid = false;
    facts.csrf_valid = false;
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_SESSION);
    CHECK(web_guard_evaluate(&READ, &facts) == WEB_GUARD_REJECT_SESSION);
    CHECK(web_guard_evaluate(&WEBSOCKET, &facts) == WEB_GUARD_REJECT_SESSION);
    CHECK(web_guard_evaluate(&SESSION_CREATE, &facts) == WEB_GUARD_ALLOW);

    facts = good();
    facts.csrf_valid = false;
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_CSRF);
}

static void test_body_limit_and_lock(void)
{
    web_guard_facts_t facts = good();
    facts.body_len = 513;
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_BODY_TOO_LARGE);
    facts.body_len = 1;
    CHECK(web_guard_evaluate(&READ, &facts) == WEB_GUARD_REJECT_BODY_TOO_LARGE);

    facts = good();
    facts.locked = true;
    CHECK(web_guard_evaluate(&WRITE, &facts) == WEB_GUARD_REJECT_LOCKED);
    CHECK(web_guard_evaluate(&SESSION_CREATE, &facts) == WEB_GUARD_REJECT_LOCKED);
    CHECK(web_guard_evaluate(&WEBSOCKET, &facts) == WEB_GUARD_REJECT_LOCKED);
    CHECK(web_guard_evaluate(&PUBLIC, &facts) == WEB_GUARD_ALLOW);
}

static void test_missing_inputs_fail_closed(void)
{
    const web_guard_facts_t facts = good();
    CHECK(web_guard_evaluate(NULL, &facts) != WEB_GUARD_ALLOW);
    CHECK(web_guard_evaluate(&PUBLIC, NULL) != WEB_GUARD_ALLOW);
}

static void test_status_lines(void)
{
    CHECK(strcmp(web_guard_status(WEB_GUARD_REJECT_SESSION), "401 Unauthorized") == 0);
    CHECK(strcmp(web_guard_status(WEB_GUARD_REJECT_CSRF), "403 Forbidden") == 0);
    CHECK(strcmp(web_guard_status(WEB_GUARD_REJECT_HOST), "421 Misdirected Request") == 0);
    CHECK(strcmp(web_guard_status(WEB_GUARD_REJECT_LOCKED), "423 Locked") == 0);
    CHECK(strcmp(web_guard_status(WEB_GUARD_REJECT_RATE_LIMIT), "429 Too Many Requests") == 0);
}

int main(void)
{
    test_everything_valid_is_allowed();
    test_rate_limit_and_host_apply_to_every_route();
    test_mutations_need_origin_session_and_csrf();
    test_body_limit_and_lock();
    test_missing_inputs_fail_closed();
    test_status_lines();
    return 0;
}
