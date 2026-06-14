#include "emergency_lock_gesture.h"

#include <string.h>

void emergency_lock_gesture_init(emergency_lock_gesture_t *gesture)
{
    if (gesture != NULL) {
        memset(gesture, 0, sizeof(*gesture));
    }
}

bool emergency_lock_gesture_update(emergency_lock_gesture_t *gesture, bool pressed, uint64_t now_ms)
{
    if (gesture == NULL) {
        return false;
    }

    if (!pressed) {
        gesture->was_pressed = false;
        gesture->triggered = false;
        gesture->press_start_ms = 0;
        return false;
    }

    if (!gesture->was_pressed) {
        gesture->was_pressed = true;
        gesture->triggered = false;
        gesture->press_start_ms = now_ms;
        return false;
    }

    if (!gesture->triggered && now_ms - gesture->press_start_ms >= EMERGENCY_LOCK_GESTURE_HOLD_MS) {
        gesture->triggered = true;
        return true;
    }

    return false;
}

