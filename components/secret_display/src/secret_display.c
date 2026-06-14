#include "secret_display.h"

#include <stdio.h>
#include <string.h>

bool secret_display_text(const char *secret, bool reveal, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    const char *text = reveal && secret != NULL && secret[0] != '\0' ? secret : SECRET_DISPLAY_MASKED_TEXT;
    const size_t len = strnlen(text, out_size);
    if (len >= out_size) {
        out[0] = '\0';
        return false;
    }

    memcpy(out, text, len + 1);
    return true;
}

bool secret_display_reveal_hint(uint64_t remaining_ms, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    const uint64_t remaining_s = remaining_ms == 0 ? 0 : ((remaining_ms + 999ULL) / 1000ULL);
    const int len = snprintf(out, out_size, "Visible %llus", (unsigned long long)remaining_s);
    if (len < 0 || len >= (int)out_size) {
        out[0] = '\0';
        return false;
    }
    return true;
}
