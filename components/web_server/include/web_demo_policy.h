#pragma once

#include <stdbool.h>

#include "web_security.h"

bool web_demo_policy_can_start(bool usb_connected, web_security_writer_state_t writer_state);
bool web_demo_policy_can_acquire_writer(bool demo_active);
