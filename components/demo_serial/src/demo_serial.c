#include "demo_serial.h"

#include <stdio.h>
#include <string.h>

static const char *const DEMO_LINES[] = {
    "\r\nSerialite demo console\r\n",
    "Linux rescue-host 6.8.0-demo #1 SMP PREEMPT_DYNAMIC x86_64\r\n",
    "ttyS0: serial console active at 115200 bps\r\n",
    "login: root\r\n",
    "root@rescue-host:~# ip addr show eth0\r\n",
    "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500\r\n",
    "    inet 10.0.42.18/24 scope global eth0\r\n",
    "root@rescue-host:~# journalctl -xb -n 3\r\n",
    "kernel: demo: network recovered, ssh still unavailable\r\n",
    "systemd: demo-rescue.service entered running state\r\n",
};

void demo_serial_init(demo_serial_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

bool demo_serial_start(demo_serial_state_t *state, bool real_usb_connected, bool writer_active, uint64_t now_ms)
{
    if (state == NULL || real_usb_connected || writer_active) {
        return false;
    }

    state->active = true;
    state->next_line = 0;
    state->next_emit_ms = now_ms;
    state->bytes_emitted = 0;
    return true;
}

void demo_serial_stop(demo_serial_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->active = false;
}

bool demo_serial_is_active(const demo_serial_state_t *state)
{
    return state != NULL && state->active;
}

size_t demo_serial_next_output(demo_serial_state_t *state, uint64_t now_ms, char *out, size_t out_size)
{
    if (state == NULL || out == NULL || out_size == 0 || !state->active || now_ms < state->next_emit_ms) {
        return 0;
    }

    const char *line = DEMO_LINES[state->next_line % (sizeof(DEMO_LINES) / sizeof(DEMO_LINES[0]))];
    const int written = snprintf(out, out_size, "%s", line);
    if (written < 0) {
        return 0;
    }

    const size_t len = (size_t)written < out_size ? (size_t)written : out_size - 1;
    state->next_line++;
    state->next_emit_ms = now_ms + DEMO_SERIAL_INTERVAL_MS;
    state->bytes_emitted += len;
    return len;
}

uint64_t demo_serial_bytes_emitted(const demo_serial_state_t *state)
{
    return state == NULL ? 0 : state->bytes_emitted;
}
