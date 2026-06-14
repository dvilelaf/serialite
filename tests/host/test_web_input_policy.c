#include <stdio.h>
#include <stdlib.h>

#include "web_input_policy.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_accepts_small_frames(void)
{
    web_input_policy_state_t state;
    web_input_policy_init(&state);

    CHECK(web_input_policy_evaluate(&state, 32, 1000) == WEB_INPUT_POLICY_ACCEPT);
    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX, 1001) == WEB_INPUT_POLICY_ACCEPT);
}

static void test_rejects_empty_and_oversized_frames(void)
{
    web_input_policy_state_t state;
    web_input_policy_init(&state);

    CHECK(web_input_policy_evaluate(&state, 0, 1000) == WEB_INPUT_POLICY_REJECT_EMPTY);
    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX + 1, 1000) == WEB_INPUT_POLICY_REJECT_TOO_LARGE);
}

static void test_rate_limits_repeated_frames_in_window(void)
{
    web_input_policy_state_t state;
    web_input_policy_init(&state);

    for (size_t i = 0; i < WEB_INPUT_POLICY_MAX_FRAMES_PER_WINDOW; ++i) {
        CHECK(web_input_policy_evaluate(&state, 1, 1000 + i) == WEB_INPUT_POLICY_ACCEPT);
    }

    CHECK(web_input_policy_evaluate(&state, 1, 1200) == WEB_INPUT_POLICY_REJECT_RATE_LIMIT);
    CHECK(web_input_policy_evaluate(&state, 1, 2001) == WEB_INPUT_POLICY_ACCEPT);
}

static void test_rate_limits_bytes_in_window(void)
{
    web_input_policy_state_t state;
    web_input_policy_init(&state);

    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX, 1000) == WEB_INPUT_POLICY_ACCEPT);
    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX, 1001) == WEB_INPUT_POLICY_ACCEPT);
    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX, 1002) == WEB_INPUT_POLICY_ACCEPT);
    CHECK(web_input_policy_evaluate(&state, WEB_INPUT_POLICY_FRAME_MAX, 1003) == WEB_INPUT_POLICY_ACCEPT);
    CHECK(web_input_policy_evaluate(&state, 1, 1004) == WEB_INPUT_POLICY_REJECT_RATE_LIMIT);
}

int main(void)
{
    test_accepts_small_frames();
    test_rejects_empty_and_oversized_frames();
    test_rate_limits_repeated_frames_in_window();
    test_rate_limits_bytes_in_window();
    return 0;
}
