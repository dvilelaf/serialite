#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EMERGENCY_LOCK_GESTURE_HOLD_MS 3000ULL

typedef struct {
    bool armed;
    bool was_pressed;
    bool triggered;
    uint64_t press_start_ms;
} emergency_lock_gesture_t;

void emergency_lock_gesture_init(emergency_lock_gesture_t *gesture);
bool emergency_lock_gesture_update(emergency_lock_gesture_t *gesture, bool pressed, uint64_t now_ms);
