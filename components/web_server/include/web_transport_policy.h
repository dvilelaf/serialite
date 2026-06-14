#pragma once

#include <stdbool.h>

typedef enum {
    WEB_TRANSPORT_UNAVAILABLE = 0,
    WEB_TRANSPORT_HTTP,
    WEB_TRANSPORT_HTTPS,
} web_transport_t;

web_transport_t web_transport_from_status(bool web_started, bool tls_active);
const char *web_transport_scheme(web_transport_t transport);
bool web_transport_should_advertise_mdns(web_transport_t transport);
