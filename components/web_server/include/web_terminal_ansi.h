#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WEB_TERMINAL_ANSI_TEXT = 0,
    WEB_TERMINAL_ANSI_ESC,
    WEB_TERMINAL_ANSI_CSI,
    WEB_TERMINAL_ANSI_OSC,
    WEB_TERMINAL_ANSI_OSC_ESC,
} web_terminal_ansi_mode_t;

typedef struct {
    web_terminal_ansi_mode_t mode;
    uint8_t sequence[64];
    size_t sequence_len;
    bool sequence_dropped;
} web_terminal_ansi_state_t;

void web_terminal_ansi_init(web_terminal_ansi_state_t *state);
size_t web_terminal_ansi_filter(
    web_terminal_ansi_state_t *state,
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_len);
