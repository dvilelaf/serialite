#include <stdio.h>
#include <stdlib.h>

#include "http_rate_limit.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_allows_requests_inside_budget(void)
{
    http_rate_limit_state_t state;
    http_rate_limit_init(&state);

    for (size_t i = 0; i < HTTP_RATE_LIMIT_MAX_REQUESTS_PER_WINDOW; ++i) {
        CHECK(http_rate_limit_evaluate(&state, 1000 + (uint64_t)i) == HTTP_RATE_LIMIT_ACCEPT);
    }
}

static void test_rejects_after_budget_exhausted(void)
{
    http_rate_limit_state_t state;
    http_rate_limit_init(&state);

    for (size_t i = 0; i < HTTP_RATE_LIMIT_MAX_REQUESTS_PER_WINDOW; ++i) {
        CHECK(http_rate_limit_evaluate(&state, 2000) == HTTP_RATE_LIMIT_ACCEPT);
    }

    CHECK(http_rate_limit_evaluate(&state, 2000) == HTTP_RATE_LIMIT_REJECT_RATE_LIMIT);
}

static void test_resets_budget_after_window(void)
{
    http_rate_limit_state_t state;
    http_rate_limit_init(&state);

    for (size_t i = 0; i < HTTP_RATE_LIMIT_MAX_REQUESTS_PER_WINDOW; ++i) {
        CHECK(http_rate_limit_evaluate(&state, 3000) == HTTP_RATE_LIMIT_ACCEPT);
    }

    CHECK(http_rate_limit_evaluate(&state, 3000 + HTTP_RATE_LIMIT_WINDOW_MS - 1) == HTTP_RATE_LIMIT_REJECT_RATE_LIMIT);
    CHECK(http_rate_limit_evaluate(&state, 3000 + HTTP_RATE_LIMIT_WINDOW_MS) == HTTP_RATE_LIMIT_ACCEPT);
}

static void test_handles_time_moving_backwards_by_starting_new_window(void)
{
    http_rate_limit_state_t state;
    http_rate_limit_init(&state);

    CHECK(http_rate_limit_evaluate(&state, 5000) == HTTP_RATE_LIMIT_ACCEPT);
    CHECK(http_rate_limit_evaluate(&state, 1000) == HTTP_RATE_LIMIT_ACCEPT);
}

int main(void)
{
    test_allows_requests_inside_budget();
    test_rejects_after_budget_exhausted();
    test_resets_budget_after_window();
    test_handles_time_moving_backwards_by_starting_new_window();
    return 0;
}
