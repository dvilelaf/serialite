#include <stdio.h>
#include <stdlib.h>

#include "http_route_policy.h"
#include "ota_update_policy.h"

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
    CHECK(http_route_policy_allowed("/ota", HTTP_ROUTE_METHOD_GET, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/write/acquire", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/write/release", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/ota", HTTP_ROUTE_METHOD_POST, OTA_UPDATE_MAX_IMAGE_BYTES) == HTTP_ROUTE_POLICY_ALLOW);
    CHECK(http_route_policy_allowed("/api/reboot", HTTP_ROUTE_METHOD_POST, 0) == HTTP_ROUTE_POLICY_ALLOW);
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
    CHECK(http_route_policy_allowed("/api/ota", HTTP_ROUTE_METHOD_POST, OTA_UPDATE_MAX_IMAGE_BYTES + 1) == HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE);
    CHECK(http_route_policy_allowed("/api/reboot", HTTP_ROUTE_METHOD_POST, 1) == HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE);
}

static void test_security_classifies_public_and_authenticated_routes(void)
{
    CHECK(http_route_policy_security("/login", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_PUBLIC);
    CHECK(http_route_policy_security("/login", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_PUBLIC);

    CHECK(http_route_policy_security("/", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/terminal", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/terminal-status.json", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/diagnostics", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/diagnostics.json", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/about", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/runbook", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
    CHECK(http_route_policy_security("/ota", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_AUTH_REQUIRED);
}

static void test_security_classifies_mutating_and_websocket_routes(void)
{
    CHECK(http_route_policy_security("/logout", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_MUTATING_AUTH_CSRF_ORIGIN);
    CHECK(http_route_policy_security("/api/write/acquire", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_MUTATING_AUTH_CSRF_ORIGIN);
    CHECK(http_route_policy_security("/api/write/release", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_MUTATING_AUTH_CSRF_ORIGIN);
    CHECK(http_route_policy_security("/api/ota", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_MUTATING_AUTH_CSRF_ORIGIN);
    CHECK(http_route_policy_security("/api/reboot", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_MUTATING_AUTH_CSRF_ORIGIN);
    CHECK(http_route_policy_security("/ws", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_WEBSOCKET_AUTH_ORIGIN);
}

static void test_security_rejects_unknown_or_wrong_method_routes(void)
{
    CHECK(http_route_policy_security("/admin", HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_UNKNOWN);
    CHECK(http_route_policy_security("/terminal", HTTP_ROUTE_METHOD_POST) == HTTP_ROUTE_SECURITY_UNKNOWN);
    CHECK(http_route_policy_security(NULL, HTTP_ROUTE_METHOD_GET) == HTTP_ROUTE_SECURITY_UNKNOWN);
}

int main(void)
{
    test_allows_known_routes_and_methods();
    test_rejects_unknown_routes_and_wrong_methods();
    test_rejects_oversized_bodies();
    test_security_classifies_public_and_authenticated_routes();
    test_security_classifies_mutating_and_websocket_routes();
    test_security_rejects_unknown_or_wrong_method_routes();
    return 0;
}
