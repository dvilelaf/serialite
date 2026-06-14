#include "http_rate_limit.h"

#include <stddef.h>

void http_rate_limit_init(http_rate_limit_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->window_started_ms = 0;
    state->requests_in_window = 0;
    state->initialized = false;
}

http_rate_limit_result_t http_rate_limit_evaluate(http_rate_limit_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return HTTP_RATE_LIMIT_REJECT_RATE_LIMIT;
    }

    if (!state->initialized ||
        now_ms < state->window_started_ms ||
        now_ms - state->window_started_ms >= HTTP_RATE_LIMIT_WINDOW_MS) {
        state->window_started_ms = now_ms;
        state->requests_in_window = 0;
        state->initialized = true;
    }

    if (state->requests_in_window >= HTTP_RATE_LIMIT_MAX_REQUESTS_PER_WINDOW) {
        return HTTP_RATE_LIMIT_REJECT_RATE_LIMIT;
    }

    state->requests_in_window++;
    return HTTP_RATE_LIMIT_ACCEPT;
}
