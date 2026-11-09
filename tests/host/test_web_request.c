#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_request.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static const char *const HOSTS[] = {"192.168.4.1", "serialite.local"};

static web_request_site_t site(uint16_t port, bool tls)
{
    return (web_request_site_t){.hosts = HOSTS, .host_count = 2, .port = port, .tls = tls};
}

static void test_host_allowlist(void)
{
    const web_request_site_t http = site(80, false);
    CHECK(web_request_host_allowed(&http, "192.168.4.1"));
    CHECK(web_request_host_allowed(&http, "192.168.4.1:80"));
    CHECK(web_request_host_allowed(&http, "Serialite.LOCAL"));
    CHECK(!web_request_host_allowed(&http, "192.168.4.1:8080"));
    CHECK(!web_request_host_allowed(&http, "evil.example"));
    CHECK(!web_request_host_allowed(&http, "evil.example:80"));
    CHECK(!web_request_host_allowed(&http, "serialite.local.evil.example"));
    CHECK(!web_request_host_allowed(&http, "192.168.4.10"));
    CHECK(!web_request_host_allowed(&http, "192.168.4.1:"));
    CHECK(!web_request_host_allowed(&http, "192.168.4.1:080"));
    CHECK(!web_request_host_allowed(&http, "192.168.4.1:99999"));
    CHECK(!web_request_host_allowed(&http, ""));
    CHECK(!web_request_host_allowed(&http, NULL));
    CHECK(!web_request_host_allowed(NULL, "192.168.4.1"));

    const web_request_site_t harness = site(8080, false);
    CHECK(web_request_host_allowed(&harness, "192.168.4.1:8080"));
    /* Without a port the client still reached this socket, so the name decides. */
    CHECK(web_request_host_allowed(&harness, "192.168.4.1"));
    CHECK(!web_request_host_allowed(&harness, "192.168.4.1:80"));
}

static void test_origin_allowlist(void)
{
    const web_request_site_t http = site(80, false);
    CHECK(web_request_origin_allowed(&http, "http://192.168.4.1"));
    CHECK(web_request_origin_allowed(&http, "http://192.168.4.1:80"));
    CHECK(web_request_origin_allowed(&http, "http://serialite.local"));
    CHECK(!web_request_origin_allowed(&http, "https://192.168.4.1"));
    CHECK(!web_request_origin_allowed(&http, "http://evil.example"));
    CHECK(!web_request_origin_allowed(&http, "http://192.168.4.1.evil.example"));
    CHECK(!web_request_origin_allowed(&http, "http://192.168.4.1/path"));
    CHECK(!web_request_origin_allowed(&http, "null"));
    CHECK(!web_request_origin_allowed(&http, ""));
    CHECK(!web_request_origin_allowed(&http, NULL));

    const web_request_site_t https = site(443, true);
    CHECK(web_request_origin_allowed(&https, "https://serialite.local"));
    CHECK(web_request_origin_allowed(&https, "https://serialite.local:443"));
    CHECK(!web_request_origin_allowed(&https, "http://serialite.local"));
    CHECK(!web_request_origin_allowed(&https, "https://serialite.local:8443"));
}

static void test_cookie_lookup_uses_exact_names(void)
{
    char value[40];
    CHECK(web_request_cookie_value("serialite_session=abc", "serialite_session", value, sizeof(value)));
    CHECK(strcmp(value, "abc") == 0);
    CHECK(web_request_cookie_value("theme=dark; serialite_session=xyz; other=1", "serialite_session", value, sizeof(value)));
    CHECK(strcmp(value, "xyz") == 0);
    CHECK(!web_request_cookie_value("evil_serialite_session=abc", "serialite_session", value, sizeof(value)));
    CHECK(!web_request_cookie_value("serialite_sessionx=abc", "serialite_session", value, sizeof(value)));
    CHECK(!web_request_cookie_value("theme=dark", "serialite_session", value, sizeof(value)));
    CHECK(!web_request_cookie_value("serialite_session=0123456789", "serialite_session", value, 5));
    CHECK(!web_request_cookie_value(NULL, "serialite_session", value, sizeof(value)));
}

static void test_secret_compare(void)
{
    CHECK(web_request_secret_equal("abcd", "abcd", 4));
    CHECK(!web_request_secret_equal("abcd", "abce", 4));
    CHECK(!web_request_secret_equal(NULL, "abcd", 4));
}

int main(void)
{
    test_host_allowlist();
    test_origin_allowlist();
    test_cookie_lookup_uses_exact_names();
    test_secret_compare();
    return 0;
}
