#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RESET_GESTURE_HOLD_MS 10000ULL

typedef struct {
    bool was_pressed;
    bool triggered;
    uint64_t press_start_ms;
} reset_gesture_t;

void reset_gesture_init(reset_gesture_t *gesture);
bool reset_gesture_update(reset_gesture_t *gesture, bool pressed, uint64_t now_ms);
