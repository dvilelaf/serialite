#include "secret_display.h"

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
