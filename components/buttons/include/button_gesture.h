#pragma once

/* Pure press/hold detector for one button, polled with raw samples. */

#include <stdbool.h>
#include <stdint.h>

#define BUTTON_GESTURE_SHORT_MIN_MS 50U

typedef enum {
    BUTTON_GESTURE_NONE = 0,
    /* The button went down. */
    BUTTON_GESTURE_PRESSED,
    /* Released after at least BUTTON_GESTURE_SHORT_MIN_MS and before the long hold fired. */
    BUTTON_GESTURE_SHORT_RELEASE,
    /* Held for long_hold_ms; reported once per press. */
    BUTTON_GESTURE_LONG_HOLD,
} button_gesture_event_t;

typedef struct {
    uint32_t long_hold_ms;
    /* Set once the button has been seen released, so a button held at boot never fires. */
    bool armed;
    bool pressed;
    bool long_reported;
    uint64_t press_start_ms;
} button_gesture_t;

void button_gesture_init(button_gesture_t *gesture, uint32_t long_hold_ms);
button_gesture_event_t button_gesture_update(button_gesture_t *gesture, bool raw_pressed, uint64_t now_ms);
/* How long the current press has lasted, or 0 when the button is up or not armed. */
uint32_t button_gesture_held_ms(const button_gesture_t *gesture, uint64_t now_ms);
