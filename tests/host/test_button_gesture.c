#include <stdio.h>
#include <stdlib.h>

#include "button_gesture.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

#define LONG_MS 3000U

static button_gesture_t armed_gesture(void)
{
    button_gesture_t gesture;
    button_gesture_init(&gesture, LONG_MS);
    CHECK(button_gesture_update(&gesture, false, 0) == BUTTON_GESTURE_NONE);
    return gesture;
}

static void test_press_then_short_release(void)
{
    button_gesture_t gesture = armed_gesture();
    CHECK(button_gesture_update(&gesture, true, 1000) == BUTTON_GESTURE_PRESSED);
    CHECK(button_gesture_update(&gesture, true, 1100) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_held_ms(&gesture, 1100) == 100);
    CHECK(button_gesture_update(&gesture, false, 1200) == BUTTON_GESTURE_SHORT_RELEASE);
    CHECK(button_gesture_held_ms(&gesture, 1200) == 0);
}

static void test_bounce_is_not_a_short_release(void)
{
    button_gesture_t gesture = armed_gesture();
    CHECK(button_gesture_update(&gesture, true, 2000) == BUTTON_GESTURE_PRESSED);
    CHECK(button_gesture_update(&gesture, false, 2000 + BUTTON_GESTURE_SHORT_MIN_MS - 1) == BUTTON_GESTURE_NONE);
}

static void test_long_hold_fires_once_without_short_release(void)
{
    button_gesture_t gesture = armed_gesture();
    CHECK(button_gesture_update(&gesture, true, 3000) == BUTTON_GESTURE_PRESSED);
    CHECK(button_gesture_update(&gesture, true, 3000 + LONG_MS - 1) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_update(&gesture, true, 3000 + LONG_MS) == BUTTON_GESTURE_LONG_HOLD);
    CHECK(button_gesture_update(&gesture, true, 3000 + LONG_MS + 1000) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_update(&gesture, false, 3000 + LONG_MS + 1100) == BUTTON_GESTURE_NONE);
}

static void test_button_held_at_boot_never_fires(void)
{
    button_gesture_t gesture;
    button_gesture_init(&gesture, LONG_MS);
    CHECK(button_gesture_update(&gesture, true, 0) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_update(&gesture, true, LONG_MS + 1) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_held_ms(&gesture, LONG_MS + 1) == 0);
    CHECK(button_gesture_update(&gesture, false, LONG_MS + 2) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_update(&gesture, true, LONG_MS + 1000) == BUTTON_GESTURE_PRESSED);
    CHECK(button_gesture_update(&gesture, false, LONG_MS + 1200) == BUTTON_GESTURE_SHORT_RELEASE);
}

static void test_factory_reset_hold_length(void)
{
    button_gesture_t gesture;
    button_gesture_init(&gesture, 10000U);
    CHECK(button_gesture_update(&gesture, false, 0) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_update(&gesture, true, 100) == BUTTON_GESTURE_PRESSED);
    CHECK(button_gesture_update(&gesture, true, 9000) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_held_ms(&gesture, 9000) == 8900);
    CHECK(button_gesture_update(&gesture, true, 10100) == BUTTON_GESTURE_LONG_HOLD);
}

static void test_null_gesture_is_ignored(void)
{
    CHECK(button_gesture_update(NULL, true, 0) == BUTTON_GESTURE_NONE);
    CHECK(button_gesture_held_ms(NULL, 0) == 0);
}

int main(void)
{
    test_press_then_short_release();
    test_bounce_is_not_a_short_release();
    test_long_hold_fires_once_without_short_release();
    test_button_held_at_boot_never_fires();
    test_factory_reset_hold_length();
    test_null_gesture_is_ignored();
    return 0;
}
