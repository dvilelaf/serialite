#include "web_transport_policy.h"

#include <assert.h>
#include <string.h>

static void unavailable_when_web_did_not_start(void)
{
    const web_transport_t transport = web_transport_from_status(false, true);
    assert(transport == WEB_TRANSPORT_UNAVAILABLE);
    assert(!web_transport_should_advertise_mdns(transport));
    assert(strcmp(web_transport_scheme(transport), "") == 0);
}

static void advertises_https_only_when_started_and_tls_active(void)
{
    const web_transport_t transport = web_transport_from_status(true, true);
    assert(transport == WEB_TRANSPORT_HTTPS);
    assert(web_transport_should_advertise_mdns(transport));
    assert(strcmp(web_transport_scheme(transport), "https") == 0);
}

static void advertises_http_when_started_without_tls(void)
{
    const web_transport_t transport = web_transport_from_status(true, false);
    assert(transport == WEB_TRANSPORT_HTTP);
    assert(web_transport_should_advertise_mdns(transport));
    assert(strcmp(web_transport_scheme(transport), "http") == 0);
}

int main(void)
{
    unavailable_when_web_did_not_start();
    advertises_https_only_when_started_and_tls_active();
    advertises_http_when_started_without_tls();
    return 0;
}
