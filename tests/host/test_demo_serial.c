#include "demo_serial.h"

#include <assert.h>
#include <string.h>

static void rejects_demo_when_real_usb_is_connected(void)
{
    demo_serial_state_t state;
    demo_serial_init(&state);

    assert(!demo_serial_start(&state, true, false, 1000));
    assert(!demo_serial_is_active(&state));
}

static void rejects_demo_when_writer_is_active(void)
{
    demo_serial_state_t state;
    demo_serial_init(&state);

    assert(!demo_serial_start(&state, false, true, 1000));
    assert(!demo_serial_is_active(&state));
}

static void emits_deterministic_console_output_while_active(void)
{
    demo_serial_state_t state;
    demo_serial_init(&state);

    assert(demo_serial_start(&state, false, false, 1000));
    assert(demo_serial_is_active(&state));

    char out[256];
    const size_t first = demo_serial_next_output(&state, 1000, out, sizeof(out));
    assert(first > 0);
    assert(strstr(out, "ESP32-KVM demo console") != NULL);

    const size_t too_early = demo_serial_next_output(&state, 1100, out, sizeof(out));
    assert(too_early == 0);

    const size_t second = demo_serial_next_output(&state, 2500, out, sizeof(out));
    assert(second > 0);
    assert(strstr(out, "Linux rescue-host") != NULL);

    assert(demo_serial_next_output(&state, 3700, out, sizeof(out)) > 0);
    assert(strstr(out, "ttyS0") != NULL);
    assert(demo_serial_next_output(&state, 4900, out, sizeof(out)) > 0);
    assert(strstr(out, "login: root") != NULL);
}

static void stop_clears_active_state(void)
{
    demo_serial_state_t state;
    demo_serial_init(&state);

    assert(demo_serial_start(&state, false, false, 1000));
    demo_serial_stop(&state);
    assert(!demo_serial_is_active(&state));
}

int main(void)
{
    rejects_demo_when_real_usb_is_connected();
    rejects_demo_when_writer_is_active();
    emits_deterministic_console_output_while_active();
    stop_clears_active_state();
    return 0;
}
