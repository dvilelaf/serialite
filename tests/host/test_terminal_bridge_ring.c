#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "terminal_bridge_ring.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_write_then_read_preserves_fifo_order(void)
{
    uint8_t storage[8];
    terminal_bridge_ring_t ring;
    CHECK(terminal_bridge_ring_init(&ring, storage, sizeof(storage)) == TERMINAL_BRIDGE_OK);

    const uint8_t input[] = {'a', 'b', 'c', 'd'};
    CHECK(terminal_bridge_ring_write(&ring, input, sizeof(input)) == sizeof(input));
    CHECK(terminal_bridge_ring_available(&ring) == sizeof(input));

    uint8_t output[4] = {0};
    CHECK(terminal_bridge_ring_read(&ring, output, sizeof(output)) == sizeof(output));
    CHECK(memcmp(output, input, sizeof(input)) == 0);
    CHECK(terminal_bridge_ring_available(&ring) == 0);
}

static void test_write_wraps_without_reordering(void)
{
    uint8_t storage[5];
    terminal_bridge_ring_t ring;
    CHECK(terminal_bridge_ring_init(&ring, storage, sizeof(storage)) == TERMINAL_BRIDGE_OK);

    const uint8_t first[] = {'1', '2', '3', '4'};
    CHECK(terminal_bridge_ring_write(&ring, first, sizeof(first)) == sizeof(first));

    uint8_t scratch[3] = {0};
    CHECK(terminal_bridge_ring_read(&ring, scratch, sizeof(scratch)) == sizeof(scratch));

    const uint8_t second[] = {'5', '6', '7', '8'};
    CHECK(terminal_bridge_ring_write(&ring, second, sizeof(second)) == 4);

    uint8_t output[5] = {0};
    CHECK(terminal_bridge_ring_read(&ring, output, sizeof(output)) == 5);
    const uint8_t expected[] = {'4', '5', '6', '7', '8'};
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
}

static void test_overflow_drops_new_bytes_and_counts_them(void)
{
    uint8_t storage[4];
    terminal_bridge_ring_t ring;
    CHECK(terminal_bridge_ring_init(&ring, storage, sizeof(storage)) == TERMINAL_BRIDGE_OK);

    const uint8_t input[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    CHECK(terminal_bridge_ring_write(&ring, input, sizeof(input)) == sizeof(storage));
    CHECK(terminal_bridge_ring_dropped(&ring) == 2);

    uint8_t output[4] = {0};
    CHECK(terminal_bridge_ring_read(&ring, output, sizeof(output)) == sizeof(output));
    const uint8_t expected[] = {'a', 'b', 'c', 'd'};
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
}

static void test_clear_recovers_capacity_after_overflow(void)
{
    uint8_t storage[4];
    terminal_bridge_ring_t ring;
    CHECK(terminal_bridge_ring_init(&ring, storage, sizeof(storage)) == TERMINAL_BRIDGE_OK);

    const uint8_t overflow[] = {'a', 'b', 'c', 'd', 'e'};
    CHECK(terminal_bridge_ring_write(&ring, overflow, sizeof(overflow)) == sizeof(storage));
    CHECK(terminal_bridge_ring_dropped(&ring) == 1);

    terminal_bridge_ring_clear(&ring);
    CHECK(terminal_bridge_ring_available(&ring) == 0);
    CHECK(terminal_bridge_ring_capacity(&ring) == sizeof(storage));
    CHECK(terminal_bridge_ring_dropped(&ring) == 1);

    const uint8_t input[] = {'x', 'y'};
    uint8_t output[2] = {0};
    CHECK(terminal_bridge_ring_write(&ring, input, sizeof(input)) == sizeof(input));
    CHECK(terminal_bridge_ring_read(&ring, output, sizeof(output)) == sizeof(output));
    CHECK(memcmp(output, input, sizeof(input)) == 0);
}

static void test_invalid_arguments_are_rejected(void)
{
    uint8_t storage[4];
    terminal_bridge_ring_t ring;

    CHECK(terminal_bridge_ring_init(NULL, storage, sizeof(storage)) == TERMINAL_BRIDGE_ERR_INVALID_ARG);
    CHECK(terminal_bridge_ring_init(&ring, NULL, sizeof(storage)) == TERMINAL_BRIDGE_ERR_INVALID_ARG);
    CHECK(terminal_bridge_ring_init(&ring, storage, 0) == TERMINAL_BRIDGE_ERR_INVALID_ARG);

    CHECK(terminal_bridge_ring_init(&ring, storage, sizeof(storage)) == TERMINAL_BRIDGE_OK);
    CHECK(terminal_bridge_ring_write(NULL, storage, sizeof(storage)) == 0);
    CHECK(terminal_bridge_ring_write(&ring, NULL, sizeof(storage)) == 0);
    CHECK(terminal_bridge_ring_read(NULL, storage, sizeof(storage)) == 0);
    CHECK(terminal_bridge_ring_read(&ring, NULL, sizeof(storage)) == 0);
}

int main(void)
{
    test_write_then_read_preserves_fifo_order();
    test_write_wraps_without_reordering();
    test_overflow_drops_new_bytes_and_counts_them();
    test_clear_recovers_capacity_after_overflow();
    test_invalid_arguments_are_rejected();
    return 0;
}
