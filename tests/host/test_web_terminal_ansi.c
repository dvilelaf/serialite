#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_terminal_ansi.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static size_t filter_text(web_terminal_ansi_state_t *state, const char *input, char *output, size_t output_len)
{
    const size_t written = web_terminal_ansi_filter(
        state,
        (const uint8_t *)input,
        strlen(input),
        (uint8_t *)output,
        output_len - 1);
    output[written] = '\0';
    return written;
}

static void test_strips_sgr_and_cursor_csi_sequences(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "boot \x1b[31mFAIL\x1b[0m\r\n\x1b[2Jready", output, sizeof(output));

    CHECK(strcmp(output, "boot FAIL\r\nready") == 0);
}

static void test_handles_split_escape_sequences_across_chunks(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    size_t written = filter_text(&state, "A\x1b[3", output, sizeof(output));
    CHECK(strcmp(output, "A") == 0);

    written += filter_text(&state, "2mB\x1b[0mC", output + written, sizeof(output) - written);
    CHECK(strcmp(output, "ABC") == 0);
}

static void test_strips_osc_title_sequences(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "pre\x1b]0;secret title\a post", output, sizeof(output));

    CHECK(strcmp(output, "pre post") == 0);
}

static void test_preserves_safe_terminal_controls_only(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    const uint8_t input[] = {'a', '\b', 'b', '\t', 'c', '\n', 'd', '\r', 'e', 0x01, 'f'};
    char output[128];
    const size_t written = web_terminal_ansi_filter(&state, input, sizeof(input), (uint8_t *)output, sizeof(output) - 1);
    output[written] = '\0';

    CHECK(strcmp(output, "a\bb\tc\nd\ref") == 0);
}

static void test_respects_output_capacity(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[4];
    const size_t written = filter_text(&state, "abcdef", output, sizeof(output));

    CHECK(written == 3);
    CHECK(strcmp(output, "abc") == 0);
}

int main(void)
{
    test_strips_sgr_and_cursor_csi_sequences();
    test_handles_split_escape_sequences_across_chunks();
    test_strips_osc_title_sequences();
    test_preserves_safe_terminal_controls_only();
    test_respects_output_capacity();
    return 0;
}
