#include "terminal_bridge_ring.h"

terminal_bridge_err_t terminal_bridge_ring_init(
    terminal_bridge_ring_t *ring,
    uint8_t *storage,
    size_t capacity)
{
    if (ring == NULL || storage == NULL || capacity == 0) {
        return TERMINAL_BRIDGE_ERR_INVALID_ARG;
    }

    ring->storage = storage;
    ring->capacity = capacity;
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->used = 0;
    ring->dropped = 0;

    return TERMINAL_BRIDGE_OK;
}

size_t terminal_bridge_ring_write(
    terminal_bridge_ring_t *ring,
    const uint8_t *data,
    size_t len)
{
    if (ring == NULL || data == NULL || ring->storage == NULL || ring->capacity == 0) {
        return 0;
    }

    size_t written = 0;
    while (written < len && ring->used < ring->capacity) {
        ring->storage[ring->write_pos] = data[written];
        ring->write_pos = (ring->write_pos + 1) % ring->capacity;
        ring->used++;
        written++;
    }

    ring->dropped += len - written;
    return written;
}

size_t terminal_bridge_ring_read(
    terminal_bridge_ring_t *ring,
    uint8_t *data,
    size_t len)
{
    if (ring == NULL || data == NULL || ring->storage == NULL || ring->capacity == 0) {
        return 0;
    }

    size_t read = 0;
    while (read < len && ring->used > 0) {
        data[read] = ring->storage[ring->read_pos];
        ring->read_pos = (ring->read_pos + 1) % ring->capacity;
        ring->used--;
        read++;
    }

    return read;
}

size_t terminal_bridge_ring_available(const terminal_bridge_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }

    return ring->used;
}

size_t terminal_bridge_ring_capacity(const terminal_bridge_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }

    return ring->capacity;
}

size_t terminal_bridge_ring_dropped(const terminal_bridge_ring_t *ring)
{
    if (ring == NULL) {
        return 0;
    }

    return ring->dropped;
}

void terminal_bridge_ring_clear(terminal_bridge_ring_t *ring)
{
    if (ring == NULL) {
        return;
    }

    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->used = 0;
}
