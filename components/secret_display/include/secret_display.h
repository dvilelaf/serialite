#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECRET_DISPLAY_MASKED_TEXT "hidden - press BOOT"

bool secret_display_text(const char *secret, bool reveal, char *out, size_t out_size);
bool secret_display_reveal_hint(uint64_t remaining_ms, char *out, size_t out_size);
