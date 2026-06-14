#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TERMINAL_SCROLLBACK_OK = 0,
    TERMINAL_SCROLLBACK_ERR_INVALID_ARG = -1,
} terminal_scrollback_err_t;

typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t start;
    size_t count;
    uint64_t dropped_oldest;
} terminal_scrollback_t;

terminal_scrollback_err_t terminal_scrollback_init(terminal_scrollback_t *scrollback, uint8_t *storage, size_t capacity);
size_t terminal_scrollback_write(terminal_scrollback_t *scrollback, const uint8_t *data, size_t len);
size_t terminal_scrollback_snapshot(const terminal_scrollback_t *scrollback, uint8_t *out, size_t max_len);
size_t terminal_scrollback_available(const terminal_scrollback_t *scrollback);
size_t terminal_scrollback_capacity(const terminal_scrollback_t *scrollback);
uint64_t terminal_scrollback_dropped_oldest(const terminal_scrollback_t *scrollback);
