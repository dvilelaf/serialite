#include "http_route_policy.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    const char *uri;
    http_route_method_t method;
    size_t max_body_len;
} route_rule_t;

static const route_rule_t ROUTES[] = {
    {"/", HTTP_ROUTE_METHOD_GET, 0},
    {"/login", HTTP_ROUTE_METHOD_GET, 0},
    {"/login", HTTP_ROUTE_METHOD_POST, HTTP_ROUTE_LOGIN_BODY_MAX},
    {"/logout", HTTP_ROUTE_METHOD_POST, 0},
    {"/terminal", HTTP_ROUTE_METHOD_GET, 0},
    {"/terminal-status.json", HTTP_ROUTE_METHOD_GET, 0},
    {"/diagnostics", HTTP_ROUTE_METHOD_GET, 0},
    {"/diagnostics.json", HTTP_ROUTE_METHOD_GET, 0},
    {"/about", HTTP_ROUTE_METHOD_GET, 0},
    {"/runbook", HTTP_ROUTE_METHOD_GET, 0},
    {"/api/write/acquire", HTTP_ROUTE_METHOD_POST, 0},
    {"/api/write/release", HTTP_ROUTE_METHOD_POST, 0},
    {"/ws", HTTP_ROUTE_METHOD_GET, 0},
};

http_route_policy_result_t http_route_policy_allowed(const char *uri, http_route_method_t method, size_t body_len)
{
    if (uri == NULL) {
        return HTTP_ROUTE_POLICY_REJECT_NOT_FOUND;
    }

    bool uri_known = false;
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); ++i) {
        const route_rule_t *rule = &ROUTES[i];
        if (strcmp(uri, rule->uri) != 0) {
            continue;
        }
        uri_known = true;
        if (method != rule->method) {
            continue;
        }
        if (body_len > rule->max_body_len) {
            return HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE;
        }
        return HTTP_ROUTE_POLICY_ALLOW;
    }

    return uri_known ? HTTP_ROUTE_POLICY_REJECT_METHOD : HTTP_ROUTE_POLICY_REJECT_NOT_FOUND;
}

const char *http_route_policy_result_name(http_route_policy_result_t result)
{
    switch (result) {
        case HTTP_ROUTE_POLICY_ALLOW:
            return "allow";
        case HTTP_ROUTE_POLICY_REJECT_NOT_FOUND:
            return "not_found";
        case HTTP_ROUTE_POLICY_REJECT_METHOD:
            return "method";
        case HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE:
            return "body_too_large";
        default:
            return "unknown";
    }
}
