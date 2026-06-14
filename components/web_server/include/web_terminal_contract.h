#pragma once

#include <stdbool.h>

#define WEB_TERMINAL_STATUS_READ_ONLY "Read-only"
#define WEB_TERMINAL_STATUS_WRITE_ACTIVE "Write active"
#define WEB_TERMINAL_STATUS_WRITER_BUSY "Writer busy"
#define WEB_TERMINAL_STATUS_USB_DISCONNECTED "USB disconnected"
#define WEB_TERMINAL_STATUS_LOCKED "Locked"

#define WEB_TERMINAL_KEY_CTRL_C "Ctrl+C"
#define WEB_TERMINAL_KEY_CTRL_D "Ctrl+D"
#define WEB_TERMINAL_KEY_ENTER "Enter"
#define WEB_TERMINAL_KEY_ESC "Esc"
#define WEB_TERMINAL_KEY_TAB "Tab"
#define WEB_TERMINAL_KEY_UP "Up"
#define WEB_TERMINAL_KEY_DOWN "Down"
#define WEB_TERMINAL_KEY_LEFT "Left"
#define WEB_TERMINAL_KEY_RIGHT "Right"

bool web_terminal_contract_has_required_statuses(void);
bool web_terminal_contract_has_mobile_keys(void);
