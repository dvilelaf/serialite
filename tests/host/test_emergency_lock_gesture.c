#include <stdio.h>
#include <stdlib.h>

#include "emergency_lock_gesture.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_short_press_does_not_trigger(void)
{
    emergency_lock_gesture_t gesture;
    emergency_lock_gesture_init(&gesture);

    CHECK(!emergency_lock_gesture_update(&gesture, true, 1000));
    CHECK(!emergency_lock_gesture_update(&gesture, true, 1000 + EMERGENCY_LOCK_GESTURE_HOLD_MS - 1));
    CHECK(!emergency_lock_gesture_update(&gesture, false, 1000 + EMERGENCY_LOCK_GESTURE_HOLD_MS));
}

static void test_hold_triggers_once(void)
{
    emergency_lock_gesture_t gesture;
    emergency_lock_gesture_init(&gesture);

    CHECK(!emergency_lock_gesture_update(&gesture, true, 2000));
    CHECK(emergency_lock_gesture_update(&gesture, true, 2000 + EMERGENCY_LOCK_GESTURE_HOLD_MS));
    CHECK(!emergency_lock_gesture_update(&gesture, true, 2000 + EMERGENCY_LOCK_GESTURE_HOLD_MS + 1000));
}

static void test_release_rearms_gesture(void)
{
    emergency_lock_gesture_t gesture;
    emergency_lock_gesture_init(&gesture);

    CHECK(!emergency_lock_gesture_update(&gesture, true, 3000));
    CHECK(emergency_lock_gesture_update(&gesture, true, 3000 + EMERGENCY_LOCK_GESTURE_HOLD_MS));
    CHECK(!emergency_lock_gesture_update(&gesture, false, 3000 + EMERGENCY_LOCK_GESTURE_HOLD_MS + 1));
    CHECK(!emergency_lock_gesture_update(&gesture, true, 10000));
    CHECK(emergency_lock_gesture_update(&gesture, true, 10000 + EMERGENCY_LOCK_GESTURE_HOLD_MS));
}

int main(void)
{
    test_short_press_does_not_trigger();
    test_hold_triggers_once();
    test_release_rearms_gesture();
    return 0;
}

