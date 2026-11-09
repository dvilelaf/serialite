#include "button_gesture.h"

#include <stddef.h>
#include <string.h>

void button_gesture_init(button_gesture_t *gesture, uint32_t long_hold_ms)
{
    if (gesture != NULL) {
        memset(gesture, 0, sizeof(*gesture));
        gesture->long_hold_ms = long_hold_ms;
    }
}

button_gesture_event_t button_gesture_update(button_gesture_t *gesture, bool raw_pressed, uint64_t now_ms)
{
    if (gesture == NULL) {
        return BUTTON_GESTURE_NONE;
    }

    if (!raw_pressed) {
        button_gesture_event_t event = BUTTON_GESTURE_NONE;
        if (gesture->pressed && !gesture->long_reported &&
            now_ms - gesture->press_start_ms >= BUTTON_GESTURE_SHORT_MIN_MS) {
            event = BUTTON_GESTURE_SHORT_RELEASE;
        }
        gesture->armed = true;
        gesture->pressed = false;
        gesture->long_reported = false;
        gesture->press_start_ms = 0;
        return event;
    }

    if (!gesture->armed) {
        return BUTTON_GESTURE_NONE;
    }
    if (!gesture->pressed) {
        gesture->pressed = true;
        gesture->long_reported = false;
        gesture->press_start_ms = now_ms;
        return BUTTON_GESTURE_PRESSED;
    }
    if (!gesture->long_reported && now_ms - gesture->press_start_ms >= gesture->long_hold_ms) {
        gesture->long_reported = true;
        return BUTTON_GESTURE_LONG_HOLD;
    }
    return BUTTON_GESTURE_NONE;
}

uint32_t button_gesture_held_ms(const button_gesture_t *gesture, uint64_t now_ms)
{
    if (gesture == NULL || !gesture->pressed || now_ms < gesture->press_start_ms) {
        return 0;
    }
    const uint64_t held = now_ms - gesture->press_start_ms;
    return held > UINT32_MAX ? UINT32_MAX : (uint32_t)held;
}
