#pragma once

/*
 * Filters serial output before it reaches xterm.js in the browser. Drops
 * control bytes, OSC/DCS/APC/PM/SOS strings (clipboard writes, hyperlinks,
 * terminal queries) and CSI requests that make the terminal answer back into
 * the shell. Plain text and display-only escape sequences pass through.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An unfinished escape sequence is carried between calls, up to this many bytes. */
#define WEB_TERMINAL_ANSI_MAX_CARRY 64U

typedef enum {
    WEB_TERMINAL_ANSI_TEXT = 0,
    WEB_TERMINAL_ANSI_ESC,
    WEB_TERMINAL_ANSI_CSI,
    WEB_TERMINAL_ANSI_STRING,
    WEB_TERMINAL_ANSI_STRING_ESC,
} web_terminal_ansi_mode_t;

typedef struct {
    web_terminal_ansi_mode_t mode;
    uint8_t sequence[WEB_TERMINAL_ANSI_MAX_CARRY];
    size_t sequence_len;
    bool sequence_dropped;
} web_terminal_ansi_state_t;

void web_terminal_ansi_init(web_terminal_ansi_state_t *state);
/*
 * Never writes more than input_len + WEB_TERMINAL_ANSI_MAX_CARRY bytes; size
 * output_len for that or output gets truncated.
 */
size_t web_terminal_ansi_filter(
    web_terminal_ansi_state_t *state,
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_len);
