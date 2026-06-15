#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POWER_BUTTON_SHORT_MIN_MS 50ULL
#define POWER_BUTTON_LONG_MS 3000ULL

typedef enum {
    POWER_BUTTON_GESTURE_NONE = 0,
    POWER_BUTTON_GESTURE_SHORT_RELEASE,
    POWER_BUTTON_GESTURE_LONG_HOLD,
} power_button_gesture_event_t;

typedef struct {
    bool armed;
    bool was_pressed;
    bool long_reported;
    uint64_t press_start_ms;
} power_button_gesture_t;

void power_button_gesture_init(power_button_gesture_t *gesture);
power_button_gesture_event_t power_button_gesture_update(power_button_gesture_t *gesture, bool pressed, uint64_t now_ms);
