#include "web_transport_policy.h"

web_transport_t web_transport_from_status(bool web_started, bool tls_active)
{
    if (!web_started) {
        return WEB_TRANSPORT_UNAVAILABLE;
    }
    return tls_active ? WEB_TRANSPORT_HTTPS : WEB_TRANSPORT_HTTP;
}

const char *web_transport_scheme(web_transport_t transport)
{
    switch (transport) {
        case WEB_TRANSPORT_HTTP:
            return "http";
        case WEB_TRANSPORT_HTTPS:
            return "https";
        case WEB_TRANSPORT_UNAVAILABLE:
        default:
            return "";
    }
}

bool web_transport_should_advertise_mdns(web_transport_t transport)
{
    return transport == WEB_TRANSPORT_HTTP || transport == WEB_TRANSPORT_HTTPS;
}
