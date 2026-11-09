#include "web_terminal_ansi.h"

#include <stdbool.h>

void web_terminal_ansi_init(web_terminal_ansi_state_t *state)
{
    if (state != NULL) {
        state->mode = WEB_TERMINAL_ANSI_TEXT;
        state->sequence_len = 0;
        state->sequence_dropped = false;
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

/* ESC P (DCS), ESC X (SOS), ESC ] (OSC), ESC ^ (PM), ESC _ (APC) open strings ended by BEL or ST. */
static bool starts_string_sequence(uint8_t value)
{
    return value == 'P' || value == 'X' || value == ']' || value == '^' || value == '_';
}

static bool sequence_contains(const web_terminal_ansi_state_t *state, uint8_t value)
{
    for (size_t i = 0; i < state->sequence_len; ++i) {
        if (state->sequence[i] == value) {
            return true;
        }
    }
    return false;
}

/*
 * CSI sequences that make the terminal send a reply as if the operator had
 * typed it: DA (c), DSR (n), window reports (t), DECRQM ($p), XTVERSION (>q).
 */
static bool csi_requests_reply(const web_terminal_ansi_state_t *state, uint8_t final)
{
    switch (final) {
        case 'c':
        case 'n':
        case 't':
            return true;
        case 'p':
            return sequence_contains(state, '$');
        case 'q':
            return sequence_contains(state, '>');
        default:
            return false;
    }
}

static void reset_sequence(web_terminal_ansi_state_t *state)
{
    state->sequence_len = 0;
    state->sequence_dropped = false;
}

static void append_sequence(web_terminal_ansi_state_t *state, uint8_t value)
{
    if (state->sequence_len < sizeof(state->sequence)) {
        state->sequence[state->sequence_len++] = value;
    } else {
        state->sequence_dropped = true;
    }
}

static size_t write_byte(uint8_t value, uint8_t *output, size_t written, size_t output_len)
{
    if (written < output_len) {
        output[written++] = value;
    }
    return written;
}

static size_t flush_sequence(web_terminal_ansi_state_t *state, uint8_t *output, size_t written, size_t output_len)
{
    if (!state->sequence_dropped) {
        for (size_t i = 0; i < state->sequence_len; ++i) {
            written = write_byte(state->sequence[i], output, written, output_len);
        }
    }
    reset_sequence(state);
    return written;
}

size_t web_terminal_ansi_filter(
    web_terminal_ansi_state_t *state,
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_len)
{
    if (state == NULL || input == NULL || output == NULL || output_len == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < input_len; ++i) {
        const uint8_t ch = input[i];

        switch (state->mode) {
            case WEB_TERMINAL_ANSI_TEXT:
                if (ch == 0x1b) {
                    reset_sequence(state);
                    append_sequence(state, ch);
                    state->mode = WEB_TERMINAL_ANSI_ESC;
                } else if (is_safe_text_byte(ch)) {
                    written = write_byte(ch, output, written, output_len);
                }
                break;

            case WEB_TERMINAL_ANSI_ESC:
                if (ch == '[') {
                    append_sequence(state, ch);
                    state->mode = WEB_TERMINAL_ANSI_CSI;
                } else if (starts_string_sequence(ch)) {
                    reset_sequence(state);
                    state->mode = WEB_TERMINAL_ANSI_STRING;
                } else if (ch == 0x1b) {
                    reset_sequence(state);
                    append_sequence(state, ch);
                } else {
                    append_sequence(state, ch);
                    written = flush_sequence(state, output, written, output_len);
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                }
                break;

            case WEB_TERMINAL_ANSI_CSI:
                append_sequence(state, ch);
                if (csi_final_byte(ch)) {
                    if (csi_requests_reply(state, ch)) {
                        reset_sequence(state);
                    } else {
                        written = flush_sequence(state, output, written, output_len);
                    }
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                }
                break;

            case WEB_TERMINAL_ANSI_STRING:
                if (ch == '\a') {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                } else if (ch == 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_STRING_ESC;
                }
                break;

            case WEB_TERMINAL_ANSI_STRING_ESC:
                if (ch == '\\') {
                    state->mode = WEB_TERMINAL_ANSI_TEXT;
                } else if (ch != 0x1b) {
                    state->mode = WEB_TERMINAL_ANSI_STRING;
                }
                break;

            default:
                state->mode = WEB_TERMINAL_ANSI_TEXT;
                reset_sequence(state);
                break;
        }
    }

    return written;
}
