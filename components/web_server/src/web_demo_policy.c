#include "web_demo_policy.h"

bool web_demo_policy_can_start(bool usb_connected, web_security_writer_state_t writer_state)
{
    return !usb_connected && writer_state == WEB_SECURITY_WRITER_READ_ONLY;
}

bool web_demo_policy_can_acquire_writer(bool demo_active)
{
    return !demo_active;
}
