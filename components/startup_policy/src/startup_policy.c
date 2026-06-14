#include "startup_policy.h"

startup_policy_action_t startup_policy_after_ui(bool ui_ok, bool ephemeral_credentials)
{
    if (!ui_ok && ephemeral_credentials) {
        return STARTUP_POLICY_SKIP_AP;
    }
    return STARTUP_POLICY_CONTINUE;
}

startup_policy_action_t startup_policy_after_web(bool wifi_ok, bool web_ok)
{
    if (wifi_ok && !web_ok) {
        return STARTUP_POLICY_STOP_AP;
    }
    return STARTUP_POLICY_CONTINUE;
}

