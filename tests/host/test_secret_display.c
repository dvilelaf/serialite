#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "secret_display.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_masks_secret_by_default(void)
{
    char out[64];
    CHECK(secret_display_text("alpha-bravo-charlie-delta", false, out, sizeof(out)));
    CHECK(strcmp(out, SECRET_DISPLAY_MASKED_TEXT) == 0);
}

static void test_reveals_secret_when_allowed(void)
{
    char out[64];
    CHECK(secret_display_text("alpha-bravo-charlie-delta", true, out, sizeof(out)));
    CHECK(strcmp(out, "alpha-bravo-charlie-delta") == 0);
}

static void test_missing_secret_stays_masked(void)
{
    char out[64];
    CHECK(secret_display_text(NULL, true, out, sizeof(out)));
    CHECK(strcmp(out, SECRET_DISPLAY_MASKED_TEXT) == 0);

    CHECK(secret_display_text("", true, out, sizeof(out)));
    CHECK(strcmp(out, SECRET_DISPLAY_MASKED_TEXT) == 0);
}

static void test_rejects_too_small_output_buffer(void)
{
    char out[4];
    CHECK(!secret_display_text("alpha-bravo-charlie-delta", true, out, sizeof(out)));
    CHECK(out[0] == '\0');
}

static void test_reveal_hint_counts_down_seconds(void)
{
    char out[24];

    CHECK(secret_display_reveal_hint(30000, out, sizeof(out)));
    CHECK(strcmp(out, "Visible 30s") == 0);
    CHECK(secret_display_reveal_hint(29100, out, sizeof(out)));
    CHECK(strcmp(out, "Visible 30s") == 0);
    CHECK(secret_display_reveal_hint(29000, out, sizeof(out)));
    CHECK(strcmp(out, "Visible 29s") == 0);
    CHECK(secret_display_reveal_hint(1, out, sizeof(out)));
    CHECK(strcmp(out, "Visible 1s") == 0);
}

int main(void)
{
    test_masks_secret_by_default();
    test_reveals_secret_when_allowed();
    test_missing_secret_stays_masked();
    test_rejects_too_small_output_buffer();
    test_reveal_hint_counts_down_seconds();
    return 0;
}
