#pragma once

/*
 * Bounded JSON builder over a caller buffer. Errors are sticky: after an
 * overflow every call is a no-op and json_writer_finish() returns false.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *buf;
    size_t size;
    size_t len;
    bool ok;
} json_writer_t;

void json_writer_init(json_writer_t *writer, char *buf, size_t size);
/* Appends pre-formatted JSON syntax or numbers. Never pass untrusted strings here. */
void json_writer_raw(json_writer_t *writer, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Appends a quoted, escaped JSON string; NULL becomes "". */
void json_writer_string(json_writer_t *writer, const char *value);
void json_writer_bool(json_writer_t *writer, bool value);
/* True when everything fit; the buffer is always NUL-terminated. */
bool json_writer_finish(json_writer_t *writer);
