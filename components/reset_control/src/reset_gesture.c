#include "reset_gesture.h"

#include <string.h>

void reset_gesture_init(reset_gesture_t *gesture)
{
    if (gesture != NULL) {
        memset(gesture, 0, sizeof(*gesture));
    }
}

bool reset_gesture_update(reset_gesture_t *gesture, bool pressed, uint64_t now_ms)
{
    if (gesture == NULL) {
        return false;
    }

    if (!pressed) {
        gesture->armed = true;
        gesture->was_pressed = false;
        gesture->triggered = false;
        gesture->press_start_ms = 0;
        return false;
    }

    if (!gesture->armed) {
        return false;
    }

    if (!gesture->was_pressed) {
        gesture->was_pressed = true;
        gesture->triggered = false;
        gesture->press_start_ms = now_ms;
        return false;
    }

    if (!gesture->triggered && now_ms - gesture->press_start_ms >= RESET_GESTURE_HOLD_MS) {
        gesture->triggered = true;
        return true;
    }

    return false;
}
