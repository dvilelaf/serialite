#include "web_terminal_ansi.h"

#include <stdbool.h>

void web_terminal_ansi_init(web_terminal_ansi_state_t *state)
{
    if (state != 0) {
        state->mode = WEB_TERMINAL_ANSI_TEXT;
    }
}

static bool is_safe_text_byte(uint8_t value)
{
    return value >= 0x20 || value == '\r' || value == '\n' || value == '\t' || value == '\b';
}

static bool csi_final_byte(uint8_t value)
{
    return value >= 0x40 && value <= 0x7e;
}

size_t web_terminal_ansi_filter(
    web_terminal_ansi_state_t *state,
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_len)
{
    if (state == 0 || input == 0 || output == 0 || output_len == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < input_len; ++i) {
        const uint8_t ch = input[i];

        switch (state->mode) {
            case WEB_TERMINAL_ANSI_TEXT:
                if (ch == 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_ESC;
                } else if (is_safe_text_byte(ch) && written < output_len) {
                    output[written++] = ch;
                }
                break;

            case WEB_TERMINAL_ANSI_ESC:
                if (ch == '[') {
                    state->mode = WEB_TERMINAL_ANSI_CSI;
                } else if (ch == ']') {
                    state->mode = WEB_TERMINAL_ANSI_OSC;
                } else if (ch == 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_ESC;
                } else {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                }
                break;

            case WEB_TERMINAL_ANSI_CSI:
                if (csi_final_byte(ch)) {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                }
                break;

            case WEB_TERMINAL_ANSI_OSC:
                if (ch == '\a') {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                } else if (ch == 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_OSC_ESC;
                }
                break;

            case WEB_TERMINAL_ANSI_OSC_ESC:
                if (ch == '\\') {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                } else if (ch != 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_OSC;
                }
                break;

            default:
                state->mode = WEB_TERMINAL_ANSI_TEXT;
                break;
        }
    }

    return written;
}
