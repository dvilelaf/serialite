#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SECRET_DISPLAY_MASKED_TEXT "hidden - press BOOT"

bool secret_display_text(const char *secret, bool reveal, char *out, size_t out_size);
