#include "web_request.h"

#include <string.h>

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool name_equals(const char *a, size_t a_len, const char *b)
{
    const size_t b_len = strlen(b);
    if (a_len != b_len) {
        return false;
    }
    for (size_t i = 0; i < a_len; ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

static bool parse_port(const char *text, uint16_t *out)
{
    if (text[0] == '\0' || text[0] == '0') {
        return false;
    }
    uint32_t value = 0;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = (value * 10U) + (uint32_t)(*p - '0');
        if (value > 65535U) {
            return false;
        }
    }
    *out = (uint16_t)value;
    return true;
}

/* Matches "name" or "name:port" against the site, where an omitted port means default_port. */
static bool authority_allowed(const web_request_site_t *site, const char *authority, uint16_t default_port)
{
    if (site == NULL || authority == NULL || authority[0] == '\0') {
        return false;
    }

    const char *colon = strchr(authority, ':');
    const size_t name_len = colon != NULL ? (size_t)(colon - authority) : strlen(authority);
    uint16_t port = default_port;
    if (colon != NULL && !parse_port(colon + 1, &port)) {
        return false;
    }
    if (port != site->port) {
        return false;
    }
    for (size_t i = 0; i < site->host_count; ++i) {
        if (site->hosts[i] != NULL && name_equals(authority, name_len, site->hosts[i])) {
            return true;
        }
    }
    return false;
}

bool web_request_host_allowed(const web_request_site_t *site, const char *host_header)
{
    return site != NULL && authority_allowed(site, host_header, site->port);
}

bool web_request_origin_allowed(const web_request_site_t *site, const char *origin_header)
{
    if (site == NULL || origin_header == NULL) {
        return false;
    }
    const char *scheme = site->tls ? "https://" : "http://";
    const size_t scheme_len = strlen(scheme);
    if (strncmp(origin_header, scheme, scheme_len) != 0) {
        return false;
    }
    const char *authority = origin_header + scheme_len;
    if (strchr(authority, '/') != NULL) {
        return false;
    }
    return authority_allowed(site, authority, site->tls ? 443 : 80);
}

bool web_request_cookie_value(const char *cookie_header, const char *name, char *out, size_t out_size)
{
    if (cookie_header == NULL || name == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const size_t name_len = strlen(name);
    const char *cursor = cookie_header;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == ';') {
            cursor++;
        }
        const char *end = strchr(cursor, ';');
        const size_t pair_len = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (pair_len > name_len && strncmp(cursor, name, name_len) == 0 && cursor[name_len] == '=') {
            const size_t value_len = pair_len - name_len - 1U;
            if (value_len >= out_size) {
                return false;
            }
            memcpy(out, cursor + name_len + 1U, value_len);
            out[value_len] = '\0';
            return true;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
    }
    return false;
}

bool web_request_secret_equal(const char *a, const char *b, size_t len)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}
