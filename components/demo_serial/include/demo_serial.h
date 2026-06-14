#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_SERIAL_INTERVAL_MS 1200ULL

typedef struct {
    bool active;
    uint32_t next_line;
    uint64_t next_emit_ms;
    uint64_t bytes_emitted;
} demo_serial_state_t;

void demo_serial_init(demo_serial_state_t *state);
bool demo_serial_start(demo_serial_state_t *state, bool real_usb_connected, bool writer_active, uint64_t now_ms);
void demo_serial_stop(demo_serial_state_t *state);
bool demo_serial_is_active(const demo_serial_state_t *state);
size_t demo_serial_next_output(demo_serial_state_t *state, uint64_t now_ms, char *out, size_t out_size);
uint64_t demo_serial_bytes_emitted(const demo_serial_state_t *state);
