#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TERMINAL_BRIDGE_OK = 0,
    TERMINAL_BRIDGE_ERR_INVALID_ARG = -1,
} terminal_bridge_err_t;

typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t used;
    size_t dropped;
} terminal_bridge_ring_t;

/*
 * This byte ring is intentionally not thread-safe. The terminal bridge owns each
 * instance from one task; cross-task traffic must use FreeRTOS queues or a
 * synchronized wrapper before touching the ring.
 */
terminal_bridge_err_t terminal_bridge_ring_init(
    terminal_bridge_ring_t *ring,
    uint8_t *storage,
    size_t capacity);

size_t terminal_bridge_ring_write(
    terminal_bridge_ring_t *ring,
    const uint8_t *data,
    size_t len);

size_t terminal_bridge_ring_read(
    terminal_bridge_ring_t *ring,
    uint8_t *data,
    size_t len);

size_t terminal_bridge_ring_available(const terminal_bridge_ring_t *ring);

size_t terminal_bridge_ring_capacity(const terminal_bridge_ring_t *ring);

size_t terminal_bridge_ring_dropped(const terminal_bridge_ring_t *ring);

void terminal_bridge_ring_clear(terminal_bridge_ring_t *ring);
