#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HTTP_RATE_LIMIT_WINDOW_MS 1000U
#define HTTP_RATE_LIMIT_MAX_REQUESTS_PER_WINDOW 40U

typedef enum {
    HTTP_RATE_LIMIT_ACCEPT = 0,
    HTTP_RATE_LIMIT_REJECT_RATE_LIMIT,
} http_rate_limit_result_t;

typedef struct {
    uint64_t window_started_ms;
    uint32_t requests_in_window;
    bool initialized;
} http_rate_limit_state_t;

void http_rate_limit_init(http_rate_limit_state_t *state);
http_rate_limit_result_t http_rate_limit_evaluate(http_rate_limit_state_t *state, uint64_t now_ms);

