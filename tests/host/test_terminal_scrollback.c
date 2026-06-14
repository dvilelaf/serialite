#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal_scrollback.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_snapshot_preserves_recent_output(void)
{
    uint8_t storage[8];
    terminal_scrollback_t scrollback;
    CHECK(terminal_scrollback_init(&scrollback, storage, sizeof(storage)) == TERMINAL_SCROLLBACK_OK);

    const uint8_t input[] = "hello";
    terminal_scrollback_write(&scrollback, input, 5);

    uint8_t out[8] = {0};
    CHECK(terminal_scrollback_snapshot(&scrollback, out, sizeof(out)) == 5);
    CHECK(memcmp(out, "hello", 5) == 0);
}

static void test_overwrites_oldest_bytes(void)
{
    uint8_t storage[5];
    terminal_scrollback_t scrollback;
    CHECK(terminal_scrollback_init(&scrollback, storage, sizeof(storage)) == TERMINAL_SCROLLBACK_OK);

    const uint8_t input[] = "abcdefgh";
    terminal_scrollback_write(&scrollback, input, 8);

    uint8_t out[5] = {0};
    CHECK(terminal_scrollback_snapshot(&scrollback, out, sizeof(out)) == 5);
    CHECK(memcmp(out, "defgh", 5) == 0);
    CHECK(terminal_scrollback_dropped_oldest(&scrollback) == 3);
}

static void test_partial_snapshot_returns_latest_bytes(void)
{
    uint8_t storage[8];
    terminal_scrollback_t scrollback;
    CHECK(terminal_scrollback_init(&scrollback, storage, sizeof(storage)) == TERMINAL_SCROLLBACK_OK);

    const uint8_t input[] = "abcdefgh";
    terminal_scrollback_write(&scrollback, input, 8);

    uint8_t out[4] = {0};
    CHECK(terminal_scrollback_snapshot(&scrollback, out, sizeof(out)) == 4);
    CHECK(memcmp(out, "efgh", 4) == 0);
}

static void test_invalid_arguments_are_safe(void)
{
    uint8_t storage[4];
    terminal_scrollback_t scrollback;

    CHECK(terminal_scrollback_init(NULL, storage, sizeof(storage)) == TERMINAL_SCROLLBACK_ERR_INVALID_ARG);
    CHECK(terminal_scrollback_init(&scrollback, NULL, sizeof(storage)) == TERMINAL_SCROLLBACK_ERR_INVALID_ARG);
    CHECK(terminal_scrollback_init(&scrollback, storage, 0) == TERMINAL_SCROLLBACK_ERR_INVALID_ARG);

    CHECK(terminal_scrollback_init(&scrollback, storage, sizeof(storage)) == TERMINAL_SCROLLBACK_OK);
    CHECK(terminal_scrollback_write(NULL, storage, sizeof(storage)) == 0);
    CHECK(terminal_scrollback_write(&scrollback, NULL, sizeof(storage)) == 0);
    CHECK(terminal_scrollback_snapshot(NULL, storage, sizeof(storage)) == 0);
    CHECK(terminal_scrollback_snapshot(&scrollback, NULL, sizeof(storage)) == 0);
}

int main(void)
{
    test_snapshot_preserves_recent_output();
    test_overwrites_oldest_bytes();
    test_partial_snapshot_returns_latest_bytes();
    test_invalid_arguments_are_safe();
    return 0;
}
