#pragma once

#include <stdbool.h>

typedef enum {
    STARTUP_POLICY_CONTINUE = 0,
    STARTUP_POLICY_SKIP_AP,
    STARTUP_POLICY_STOP_AP,
} startup_policy_action_t;

startup_policy_action_t startup_policy_after_ui(bool ui_ok, bool ephemeral_credentials);
startup_policy_action_t startup_policy_after_web(bool wifi_ok, bool web_ok);

