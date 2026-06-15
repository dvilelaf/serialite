#include "web_terminal_contract.h"

#include <stddef.h>

static bool all_non_empty(const char *const *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == NULL || values[i][0] == '\0') {
            return false;
        }
    }
    return true;
}

bool web_terminal_contract_has_required_statuses(void)
{
    static const char *const statuses[] = {
        WEB_TERMINAL_STATUS_READ_ONLY,
        WEB_TERMINAL_STATUS_WRITE_ACTIVE,
        WEB_TERMINAL_STATUS_WRITER_BUSY,
        WEB_TERMINAL_STATUS_USB_DISCONNECTED,
        WEB_TERMINAL_STATUS_LOCKED,
    };
    return all_non_empty(statuses, sizeof(statuses) / sizeof(statuses[0]));
}

bool web_terminal_contract_has_mobile_keys(void)
{
    static const char *const keys[] = {
        WEB_TERMINAL_KEY_CTRL_C,
        WEB_TERMINAL_KEY_CTRL_D,
        WEB_TERMINAL_KEY_CTRL_L,
        WEB_TERMINAL_KEY_ENTER,
        WEB_TERMINAL_KEY_ESC,
        WEB_TERMINAL_KEY_TAB,
        WEB_TERMINAL_KEY_UP,
        WEB_TERMINAL_KEY_DOWN,
        WEB_TERMINAL_KEY_LEFT,
        WEB_TERMINAL_KEY_RIGHT,
    };
    return all_non_empty(keys, sizeof(keys) / sizeof(keys[0]));
}
