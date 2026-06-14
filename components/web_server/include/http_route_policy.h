#pragma once

#include <stddef.h>

#define HTTP_ROUTE_LOGIN_BODY_MAX 160U

typedef enum {
    HTTP_ROUTE_METHOD_GET = 0,
    HTTP_ROUTE_METHOD_POST,
    HTTP_ROUTE_METHOD_OTHER,
} http_route_method_t;

typedef enum {
    HTTP_ROUTE_POLICY_ALLOW = 0,
    HTTP_ROUTE_POLICY_REJECT_NOT_FOUND,
    HTTP_ROUTE_POLICY_REJECT_METHOD,
    HTTP_ROUTE_POLICY_REJECT_BODY_TOO_LARGE,
} http_route_policy_result_t;

http_route_policy_result_t http_route_policy_allowed(const char *uri, http_route_method_t method, size_t body_len);
const char *http_route_policy_result_name(http_route_policy_result_t result);
