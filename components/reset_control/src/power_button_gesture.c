#include "power_button_gesture.h"

#include <string.h>

void power_button_gesture_init(power_button_gesture_t *gesture)
{
    if (gesture != NULL) {
        memset(gesture, 0, sizeof(*gesture));
    }
}

power_button_gesture_event_t power_button_gesture_update(power_button_gesture_t *gesture, bool pressed, uint64_t now_ms)
{
    if (gesture == NULL) {
        return POWER_BUTTON_GESTURE_NONE;
    }

    if (!pressed) {
        power_button_gesture_event_t event = POWER_BUTTON_GESTURE_NONE;
        if (gesture->armed && gesture->was_pressed && !gesture->long_reported &&
            now_ms - gesture->press_start_ms >= POWER_BUTTON_SHORT_MIN_MS) {
            event = POWER_BUTTON_GESTURE_SHORT_RELEASE;
        }
        gesture->armed = true;
        gesture->was_pressed = false;
        gesture->long_reported = false;
        gesture->press_start_ms = 0;
        return event;
    }

    if (!gesture->armed) {
        return POWER_BUTTON_GESTURE_NONE;
    }

    if (!gesture->was_pressed) {
        gesture->was_pressed = true;
        gesture->long_reported = false;
        gesture->press_start_ms = now_ms;
        return POWER_BUTTON_GESTURE_NONE;
    }

    if (!gesture->long_reported && now_ms - gesture->press_start_ms >= POWER_BUTTON_LONG_MS) {
        gesture->long_reported = true;
        return POWER_BUTTON_GESTURE_LONG_HOLD;
    }

    return POWER_BUTTON_GESTURE_NONE;
}
