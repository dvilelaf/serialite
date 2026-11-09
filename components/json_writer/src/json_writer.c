#include "json_writer.h"

#include <stdio.h>

void json_writer_init(json_writer_t *writer, char *buf, size_t size)
{
    writer->buf = buf;
    writer->size = size;
    writer->len = 0;
    writer->ok = buf != NULL && size > 0;
    if (writer->ok) {
        buf[0] = '\0';
    }
}

static void vappend(json_writer_t *writer, const char *fmt, va_list args)
{
    if (!writer->ok) {
        return;
    }
    const size_t room = writer->size - writer->len;
    const int written = vsnprintf(writer->buf + writer->len, room, fmt, args);
    if (written < 0 || (size_t)written >= room) {
        writer->ok = false;
        writer->buf[writer->len] = '\0';
        return;
    }
    writer->len += (size_t)written;
}

static void append(json_writer_t *writer, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vappend(writer, fmt, args);
    va_end(args);
}

void json_writer_raw(json_writer_t *writer, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vappend(writer, fmt, args);
    va_end(args);
}

void json_writer_string(json_writer_t *writer, const char *value)
{
    append(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)(value != NULL ? value : ""); *p != '\0' && writer->ok; ++p) {
        switch (*p) {
            case '"':
                append(writer, "\\\"");
                break;
            case '\\':
                append(writer, "\\\\");
                break;
            case '\n':
                append(writer, "\\n");
                break;
            case '\r':
                append(writer, "\\r");
                break;
            case '\t':
                append(writer, "\\t");
                break;
            default:
                if (*p < 0x20U || *p == 0x7fU) {
                    append(writer, "\\u%04x", (unsigned)*p);
                } else {
                    append(writer, "%c", *p);
                }
                break;
        }
    }
    append(writer, "\"");
}

void json_writer_bool(json_writer_t *writer, bool value)
{
    append(writer, value ? "true" : "false");
}

bool json_writer_finish(json_writer_t *writer)
{
    return writer->ok;
}
