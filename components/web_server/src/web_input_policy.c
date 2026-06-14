#include "web_input_policy.h"

#include <string.h>

void web_input_policy_init(web_input_policy_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

web_input_policy_result_t web_input_policy_evaluate(web_input_policy_state_t *state, size_t frame_len, uint64_t now_ms)
{
    if (frame_len == 0) {
        return WEB_INPUT_POLICY_REJECT_EMPTY;
    }
    if (frame_len > WEB_INPUT_POLICY_FRAME_MAX) {
        return WEB_INPUT_POLICY_REJECT_TOO_LARGE;
    }
    if (state == NULL) {
        return WEB_INPUT_POLICY_REJECT_RATE_LIMIT;
    }

    if (state->window_start_ms == 0 || now_ms - state->window_start_ms >= WEB_INPUT_POLICY_WINDOW_MS) {
        state->window_start_ms = now_ms;
        state->frames_in_window = 0;
        state->bytes_in_window = 0;
    }

    if (state->frames_in_window >= WEB_INPUT_POLICY_MAX_FRAMES_PER_WINDOW ||
        state->bytes_in_window + frame_len > WEB_INPUT_POLICY_MAX_BYTES_PER_WINDOW) {
        return WEB_INPUT_POLICY_REJECT_RATE_LIMIT;
    }

    state->frames_in_window++;
    state->bytes_in_window += (uint32_t)frame_len;
    return WEB_INPUT_POLICY_ACCEPT;
}

const char *web_input_policy_result_name(web_input_policy_result_t result)
{
    switch (result) {
        case WEB_INPUT_POLICY_ACCEPT:
            return "accept";
        case WEB_INPUT_POLICY_REJECT_EMPTY:
            return "empty";
        case WEB_INPUT_POLICY_REJECT_TOO_LARGE:
            return "too_large";
        case WEB_INPUT_POLICY_REJECT_RATE_LIMIT:
            return "rate_limit";
        default:
            return "unknown";
    }
}
