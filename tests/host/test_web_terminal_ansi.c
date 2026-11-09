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

static void test_preserves_sgr_and_cursor_csi_sequences(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "boot \x1b[31mFAIL\x1b[0m\r\n\x1b[2Jready", output, sizeof(output));

    CHECK(strcmp(output, "boot \x1b[31mFAIL\x1b[0m\r\n\x1b[2Jready") == 0);
}

static void test_handles_split_escape_sequences_across_chunks(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    size_t written = filter_text(&state, "A\x1b[3", output, sizeof(output));
    CHECK(strcmp(output, "A") == 0);

    written += filter_text(&state, "2mB\x1b[0mC", output + written, sizeof(output) - written);
    CHECK(strcmp(output, "A\x1b[32mB\x1b[0mC") == 0);
}

static void test_preserves_full_screen_terminal_sequences(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "\x1b[?1049h\x1b[Htop\x1b[2Khtop\x1b[?25l", output, sizeof(output));

    CHECK(strcmp(output, "\x1b[?1049h\x1b[Htop\x1b[2Khtop\x1b[?25l") == 0);
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

static void test_strips_queries_that_make_the_terminal_answer(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "a\x1b[cb\x1b[>0cc\x1b[6nd\x1b[18te\x1b[?25$pf\x1b[>qg", output, sizeof(output));
    CHECK(strcmp(output, "abcdefg") == 0);

    filter_text(&state, "x\x1b[2 qy", output, sizeof(output));
    CHECK(strcmp(output, "x\x1b[2 qy") == 0);
}

static void test_strips_dcs_and_other_string_sequences(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    char output[128];
    filter_text(&state, "a\x1bP$qm\x1b\\b\x1b_apc\x1b\\c\x1b^pm\ad\x1bXsos\x1b\\e", output, sizeof(output));
    CHECK(strcmp(output, "abcde") == 0);
}

static void test_split_chunk_output_stays_within_documented_bound(void)
{
    web_terminal_ansi_state_t state;
    web_terminal_ansi_init(&state);

    uint8_t first[WEB_TERMINAL_ANSI_MAX_CARRY - 1U];
    first[0] = 0x1b;
    first[1] = '[';
    memset(first + 2, '1', sizeof(first) - 2U);
    uint8_t output[2 * WEB_TERMINAL_ANSI_MAX_CARRY + 8U];
    CHECK(web_terminal_ansi_filter(&state, first, sizeof(first), output, sizeof(output)) == 0);

    const uint8_t second[] = {'m', 'x'};
    const size_t written = web_terminal_ansi_filter(&state, second, sizeof(second), output, sizeof(second) + WEB_TERMINAL_ANSI_MAX_CARRY);
    CHECK(written == sizeof(first) + sizeof(second));
    CHECK(output[0] == 0x1b && output[written - 2U] == 'm' && output[written - 1U] == 'x');
}

int main(void)
{
    test_preserves_sgr_and_cursor_csi_sequences();
    test_handles_split_escape_sequences_across_chunks();
    test_preserves_full_screen_terminal_sequences();
    test_strips_osc_title_sequences();
    test_preserves_safe_terminal_controls_only();
    test_respects_output_capacity();
    test_strips_queries_that_make_the_terminal_answer();
    test_strips_dcs_and_other_string_sequences();
    test_split_chunk_output_stays_within_documented_bound();
    return 0;
}
