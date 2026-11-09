#pragma once

/* Pure request parsing; no ESP-IDF dependencies so it runs in host tests. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The names and port this server answers to; anything else is DNS rebinding or misrouting. */
typedef struct {
    const char *const *hosts;
    size_t host_count;
    uint16_t port;
    bool tls;
} web_request_site_t;

/* Host header: an allowed name, optionally followed by :port matching the server port. */
bool web_request_host_allowed(const web_request_site_t *site, const char *host_header);
/* Origin header: the server's own scheme plus an allowed host (and port, if not the default). */
bool web_request_origin_allowed(const web_request_site_t *site, const char *origin_header);
/* Copies the value of the cookie called exactly `name`; false if absent or it does not fit. */
bool web_request_cookie_value(const char *cookie_header, const char *name, char *out, size_t out_size);
/* Constant-time comparison for equal-length secrets. */
bool web_request_secret_equal(const char *a, const char *b, size_t len);
