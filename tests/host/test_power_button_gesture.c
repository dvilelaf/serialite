#include <stdio.h>
#include <stdlib.h>

#include "power_button_gesture.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_short_press_reports_lock_toggle_on_release(void)
{
    power_button_gesture_t gesture;
    power_button_gesture_init(&gesture);

    CHECK(power_button_gesture_update(&gesture, false, 900) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, 1000) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, 1100) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, false, 1200) == POWER_BUTTON_GESTURE_SHORT_RELEASE);
}

static void test_bounce_does_not_report_short_press(void)
{
    power_button_gesture_t gesture;
    power_button_gesture_init(&gesture);

    CHECK(power_button_gesture_update(&gesture, false, 1900) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, 2000) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, false, 2000 + POWER_BUTTON_SHORT_MIN_MS - 1) == POWER_BUTTON_GESTURE_NONE);
}

static void test_long_press_reports_shutdown_once_without_short_release(void)
{
    power_button_gesture_t gesture;
    power_button_gesture_init(&gesture);

    CHECK(power_button_gesture_update(&gesture, false, 2900) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, 3000) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, 3000 + POWER_BUTTON_LONG_MS) == POWER_BUTTON_GESTURE_LONG_HOLD);
    CHECK(power_button_gesture_update(&gesture, true, 3000 + POWER_BUTTON_LONG_MS + 1000) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, false, 3000 + POWER_BUTTON_LONG_MS + 1100) == POWER_BUTTON_GESTURE_NONE);
}

static void test_pressed_during_boot_does_not_trigger_until_released(void)
{
    power_button_gesture_t gesture;
    power_button_gesture_init(&gesture);

    CHECK(power_button_gesture_update(&gesture, true, 0) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, POWER_BUTTON_LONG_MS + 1) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, false, POWER_BUTTON_LONG_MS + 2) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, true, POWER_BUTTON_LONG_MS + 1000) == POWER_BUTTON_GESTURE_NONE);
    CHECK(power_button_gesture_update(&gesture, false, POWER_BUTTON_LONG_MS + 1200) == POWER_BUTTON_GESTURE_SHORT_RELEASE);
}

int main(void)
{
    test_short_press_reports_lock_toggle_on_release();
    test_bounce_does_not_report_short_press();
    test_long_press_reports_shutdown_once_without_short_release();
    test_pressed_during_boot_does_not_trigger_until_released();
    return 0;
}
