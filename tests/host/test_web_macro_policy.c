#include "web_macro_policy.h"

#include <assert.h>
#include <string.h>

static void known_macros_are_visible_but_bounded(void)
{
    const web_macro_descriptor_t *macros = NULL;
    const size_t count = web_macro_policy_list(&macros);

    assert(macros != NULL);
    assert(count == 3);
    for (size_t i = 0; i < count; ++i) {
        assert(macros[i].id != NULL);
        assert(macros[i].label != NULL);
        assert(macros[i].command != NULL);
        assert(strlen(macros[i].command) <= WEB_MACRO_POLICY_COMMAND_MAX);
        assert(strchr(macros[i].command, '\n') == NULL);
    }
}

static void macros_are_disabled_by_default(void)
{
    assert(!web_macro_policy_default_enabled());
    assert(!web_macro_policy_can_run("net-status", false, true, true, false, true));
}

static void macro_run_requires_safe_runtime_state(void)
{
    assert(web_macro_policy_can_run("net-status", true, true, true, false, true));
    assert(!web_macro_policy_can_run("net-status", true, false, true, false, true));
    assert(!web_macro_policy_can_run("net-status", true, true, false, false, true));
    assert(!web_macro_policy_can_run("net-status", true, true, true, true, true));
    assert(!web_macro_policy_can_run("net-status", true, true, true, false, false));
    assert(!web_macro_policy_can_run("unknown", true, true, true, false, true));
    assert(!web_macro_policy_can_run(NULL, true, true, true, false, true));
}

static void known_macro_commands_are_resolved_by_id(void)
{
    const web_macro_descriptor_t *macro = web_macro_policy_find("boot-logs");

    assert(macro != NULL);
    assert(strcmp(macro->id, "boot-logs") == 0);
    assert(strstr(macro->command, "journalctl") != NULL);
    assert(web_macro_policy_find("missing") == NULL);
}

int main(void)
{
    known_macros_are_visible_but_bounded();
    macros_are_disabled_by_default();
    macro_run_requires_safe_runtime_state();
    known_macro_commands_are_resolved_by_id();
    return 0;
}
