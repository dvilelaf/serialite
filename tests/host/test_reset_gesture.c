#include <stdio.h>
#include <stdlib.h>

#include "reset_gesture.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_short_press_does_not_trigger(void)
{
    reset_gesture_t gesture;
    reset_gesture_init(&gesture);

    CHECK(!reset_gesture_update(&gesture, false, 900));
    CHECK(!reset_gesture_update(&gesture, true, 1000));
    CHECK(!reset_gesture_update(&gesture, true, 1000 + RESET_GESTURE_HOLD_MS - 1));
    CHECK(!reset_gesture_update(&gesture, false, 1000 + RESET_GESTURE_HOLD_MS));
}

static void test_long_press_triggers_once(void)
{
    reset_gesture_t gesture;
    reset_gesture_init(&gesture);

    CHECK(!reset_gesture_update(&gesture, false, 1900));
    CHECK(!reset_gesture_update(&gesture, true, 2000));
    CHECK(reset_gesture_update(&gesture, true, 2000 + RESET_GESTURE_HOLD_MS));
    CHECK(!reset_gesture_update(&gesture, true, 2000 + RESET_GESTURE_HOLD_MS + 1000));
}

static void test_release_allows_new_long_press(void)
{
    reset_gesture_t gesture;
    reset_gesture_init(&gesture);

    CHECK(!reset_gesture_update(&gesture, false, 2900));
    CHECK(!reset_gesture_update(&gesture, true, 3000));
    CHECK(reset_gesture_update(&gesture, true, 3000 + RESET_GESTURE_HOLD_MS));
    CHECK(!reset_gesture_update(&gesture, false, 3000 + RESET_GESTURE_HOLD_MS + 1));
    CHECK(!reset_gesture_update(&gesture, true, 20000));
    CHECK(reset_gesture_update(&gesture, true, 20000 + RESET_GESTURE_HOLD_MS));
}

static void test_pressed_during_boot_does_not_trigger_until_released(void)
{
    reset_gesture_t gesture;
    reset_gesture_init(&gesture);

    CHECK(!reset_gesture_update(&gesture, true, 0));
    CHECK(!reset_gesture_update(&gesture, true, RESET_GESTURE_HOLD_MS + 1));
    CHECK(!reset_gesture_update(&gesture, false, RESET_GESTURE_HOLD_MS + 2));
    CHECK(!reset_gesture_update(&gesture, true, RESET_GESTURE_HOLD_MS + 1000));
    CHECK(reset_gesture_update(&gesture, true, RESET_GESTURE_HOLD_MS * 2 + 1000));
}

int main(void)
{
    test_short_press_does_not_trigger();
    test_long_press_triggers_once();
    test_release_allows_new_long_press();
    test_pressed_during_boot_does_not_trigger_until_released();
    return 0;
}
