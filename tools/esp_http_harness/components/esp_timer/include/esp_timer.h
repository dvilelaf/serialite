#pragma once

/* Replaces ESP-IDF's esp_timer, which has no linux-target implementation. */

#include <stdint.h>

int64_t esp_timer_get_time(void);
