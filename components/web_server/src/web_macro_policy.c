#include "web_macro_policy.h"

#include <string.h>

static const web_macro_descriptor_t MACROS[] = {
    {
        .id = "net-status",
        .label = "Network status",
        .command = "ip addr; ip route\r",
    },
    {
        .id = "failed-units",
        .label = "Failed systemd units",
        .command = "systemctl --failed --no-pager\r",
    },
    {
        .id = "boot-logs",
        .label = "Recent boot logs",
        .command = "journalctl -xb -n 80 --no-pager\r",
    },
};

size_t web_macro_policy_list(const web_macro_descriptor_t **out_macros)
{
    if (out_macros != NULL) {
        *out_macros = MACROS;
    }
    return sizeof(MACROS) / sizeof(MACROS[0]);
}

const web_macro_descriptor_t *web_macro_policy_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(MACROS) / sizeof(MACROS[0]); ++i) {
        if (strcmp(MACROS[i].id, id) == 0) {
            return &MACROS[i];
        }
    }
    return NULL;
}

bool web_macro_policy_default_enabled(void)
{
    return false;
}

bool web_macro_policy_can_run(
    const char *id,
    bool macros_enabled,
    bool writer_active,
    bool usb_connected,
    bool demo_active,
    bool user_confirmed)
{
    return macros_enabled &&
           writer_active &&
           usb_connected &&
           !demo_active &&
           user_confirmed &&
           web_macro_policy_find(id) != NULL;
}
