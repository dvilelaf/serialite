#pragma once

#include <stdbool.h>
#include <stddef.h>

#define WEB_MACRO_POLICY_COMMAND_MAX 96U

typedef struct {
    const char *id;
    const char *label;
    const char *command;
} web_macro_descriptor_t;

size_t web_macro_policy_list(const web_macro_descriptor_t **out_macros);
const web_macro_descriptor_t *web_macro_policy_find(const char *id);
bool web_macro_policy_default_enabled(void);
bool web_macro_policy_can_run(
    const char *id,
    bool macros_enabled,
    bool writer_active,
    bool usb_connected,
    bool demo_active,
    bool user_confirmed);
