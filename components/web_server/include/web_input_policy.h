#pragma once

#include <stddef.h>
#include <stdint.h>

#define WEB_INPUT_POLICY_FRAME_MAX 128U
#define WEB_INPUT_POLICY_WINDOW_MS 1000ULL
#define WEB_INPUT_POLICY_MAX_FRAMES_PER_WINDOW 32U
#define WEB_INPUT_POLICY_MAX_BYTES_PER_WINDOW 512U

typedef enum {
    WEB_INPUT_POLICY_ACCEPT = 0,
    WEB_INPUT_POLICY_REJECT_EMPTY,
    WEB_INPUT_POLICY_REJECT_TOO_LARGE,
    WEB_INPUT_POLICY_REJECT_RATE_LIMIT,
} web_input_policy_result_t;

typedef struct {
    uint64_t window_start_ms;
    uint32_t frames_in_window;
    uint32_t bytes_in_window;
} web_input_policy_state_t;

void web_input_policy_init(web_input_policy_state_t *state);
web_input_policy_result_t web_input_policy_evaluate(web_input_policy_state_t *state, size_t frame_len, uint64_t now_ms);
const char *web_input_policy_result_name(web_input_policy_result_t result);
