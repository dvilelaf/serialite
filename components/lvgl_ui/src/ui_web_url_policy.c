#include "ui_web_url_policy.h"

#include "network_identity_constants.h"

const char *ui_web_url_for_transport(bool https_enabled)
{
    return https_enabled ? NETWORK_IDENTITY_LOCAL_HTTPS_URL : NETWORK_IDENTITY_LOCAL_URL;
}
