#include <stdio.h>
#include <stdlib.h>

#include "http_route_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_allows_known_routes_and_methods(void)
{
    CHECK(http_route_policy_allowed("/", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/login", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/login", HTTP_ROUTE_METHOD_POST, 32) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/logout", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/terminal", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/terminal-status.json", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/diagnostics", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/diagnostics.json", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/about", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/runbook", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/write/acquire", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/write/release", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/ws", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
}

static void test_rejects_unknown_routes_and_wrong_methods(void)
{
    CHECK(http_route_policy_allowed("/admin", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_REJECT_NOT_FOUND);
    CHECK(http_route_policy_allowed("/terminal", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_REJECT_METHOD);
    CHECK(http_route_policy_allowed("/terminal-status.json", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_REJECT_METHOD);
    CHECK(http_route_policy_allowed("/logout", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_REJECT_METHOD);
    CHECK(http_route_policy_allowed(NULL, HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_REJECT_NOT_FOUND);
}

static void test_rejects_oversized_bodies(void)
{
    CHECK(http_route_policy_allowed("/login", HTTP_ROUTE_METHOD_POST, HTTP_ROUTE_LOGIN_BODY_MAX) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/login", HTTP_ROUTE_METHOD_POST, HTTP_ROUTE_LOGIN_BODY_MAX + 1) == HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE);
    CHECK(http_route_policy_allowed("/api/write/acquire", HTTP_ROUTE_METHOD_POST, 1) == HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE);
}

int main(void)
{
    test_allows_known_routes_and_methods();
    test_rejects_unknown_routes_and_wrong_methods();
    test_rejects_oversized_bodies();
    return 0;
}
