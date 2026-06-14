#include "terminal_scrollback.h"

terminal_scrollback_err_t terminal_scrollback_init(terminal_scrollback_t *scrollback, uint8_t *storage, size_t capacity)
{
    if (scrollback == NULL || storage == NULL || capacity == 0) {
        return TERMINAL_SCROLLBACK_ERR_INVALID_ARG;
    }

    scrollback->storage = storage;
    scrollback->capacity = capacity;
    scrollback->start = 0;
    scrollback->count = 0;
    scrollback->dropped_oldest = 0;
    return TERMINAL_SCROLLBACK_OK;
}

size_t terminal_scrollback_write(terminal_scrollback_t *scrollback, const uint8_t *data, size_t len)
{
    if (scrollback == NULL || data == NULL || scrollback->storage == NULL || scrollback->capacity == 0) {
        return 0;
    }

    for (size_t i = 0; i < len; ++i) {
        if (scrollback->count < scrollback->capacity) {
            const size_t index = (scrollback->start + scrollback->count) % scrollback->capacity;
            scrollback->storage[index] = data[i];
            scrollback->count++;
        } else {
            scrollback->storage[scrollback->start] = data[i];
            scrollback->start = (scrollback->start + 1) % scrollback->capacity;
            scrollback->dropped_oldest++;
        }
    }

    return len;
}

size_t terminal_scrollback_snapshot(const terminal_scrollback_t *scrollback, uint8_t *out, size_t max_len)
{
    if (scrollback == NULL || out == NULL || scrollback->storage == NULL || scrollback->capacity == 0 || max_len == 0) {
        return 0;
    }

    const size_t to_copy = scrollback->count < max_len ? scrollback->count : max_len;
    const size_t first = scrollback->count - to_copy;
    for (size_t i = 0; i < to_copy; ++i) {
        const size_t index = (scrollback->start + first + i) % scrollback->capacity;
        out[i] = scrollback->storage[index];
    }
    return to_copy;
}

size_t terminal_scrollback_available(const terminal_scrollback_t *scrollback)
{
    return scrollback != NULL ? scrollback->count : 0;
}

size_t terminal_scrollback_capacity(const terminal_scrollback_t *scrollback)
{
    return scrollback != NULL ? scrollback->capacity : 0;
}

uint64_t terminal_scrollback_dropped_oldest(const terminal_scrollback_t *scrollback)
{
    return scrollback != NULL ? scrollback->dropped_oldest : 0;
}
