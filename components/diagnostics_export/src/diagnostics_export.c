#include "diagnostics_export.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *level_name(event_log_level_t level)
{
    switch (level) {
        case EVENT_LOG_INFO:
            return "info";
        case EVENT_LOG_WARN:
            return "warn";
        case EVENT_LOG_ERROR:
            return "error";
        case EVENT_LOG_SECURITY:
            return "security";
        default:
            return "unknown";
    }
}

static bool appendf(char *out, size_t out_size, size_t *offset, const char *fmt, ...)
{
    if (out == NULL || offset == NULL || *offset >= out_size) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(out + *offset, out_size - *offset, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= out_size - *offset) {
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static bool append_json_string(char *out, size_t out_size, size_t *offset, const char *value)
{
    if (!appendf(out, out_size, offset, "\"")) {
        return false;
    }

    if (value == NULL) {
        value = "";
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case '"':
                if (!appendf(out, out_size, offset, "\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!appendf(out, out_size, offset, "\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!appendf(out, out_size, offset, "\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!appendf(out, out_size, offset, "\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!appendf(out, out_size, offset, "\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!appendf(out, out_size, offset, "\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!appendf(out, out_size, offset, "\\t")) {
                    return false;
                }
                break;
            default:
                if (*cursor < 0x20) {
                    if (!appendf(out, out_size, offset, "\\u%04x", (unsigned)*cursor)) {
                        return false;
                    }
                } else if (!appendf(out, out_size, offset, "%c", *cursor)) {
                    return false;
                }
                break;
        }
    }

    return appendf(out, out_size, offset, "\"");
}

static bool ascii_ieq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static bool contains_case_insensitive(const char *value, const char *needle)
{
    if (value == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    const size_t needle_len = strlen(needle);
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (matched < needle_len && cursor[matched] != '\0' && ascii_ieq(cursor[matched], needle[matched])) {
            matched++;
        }
        if (matched == needle_len) {
            return true;
        }
    }
    return false;
}

static bool message_may_contain_secret(const char *message)
{
    static const char *sensitive_terms[] = {
        "password",
        "passwd",
        "secret",
        "token",
        "cookie",
        "session",
        "credential",
        "authorization",
    };

    for (size_t i = 0; i < sizeof(sensitive_terms) / sizeof(sensitive_terms[0]); ++i) {
        if (contains_case_insensitive(message, sensitive_terms[i])) {
            return true;
        }
    }
    return false;
}

static const char *event_message_for_export(const char *message)
{
    if (message_may_contain_secret(message)) {
        return "[redacted sensitive event]";
    }
    return message != NULL ? message : "";
}

int diagnostics_export_write_html_escaped(char *out, size_t out_size, const char *value)
{
    if (out == NULL || out_size == 0) {
        return -1;
    }

    out[0] = '\0';
    size_t offset = 0;
    value = event_message_for_export(value);

    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        const char *escaped = NULL;
        switch (*cursor) {
            case '&':
                escaped = "&amp;";
                break;
            case '<':
                escaped = "&lt;";
                break;
            case '>':
                escaped = "&gt;";
                break;
            case '"':
                escaped = "&quot;";
                break;
            case '\'':
                escaped = "&#39;";
                break;
            default:
                break;
        }

        if (escaped != NULL) {
            if (!appendf(out, out_size, &offset, "%s", escaped)) {
                return -1;
            }
        } else if (!appendf(out, out_size, &offset, "%c", *cursor)) {
            return -1;
        }
    }

    return (int)offset;
}

int diagnostics_export_write_json(char *out, size_t out_size, const diagnostics_export_snapshot_t *snapshot)
{
    if (out == NULL || out_size == 0 || snapshot == NULL) {
        return -1;
    }
    if (snapshot->event_count > 0 && snapshot->events == NULL) {
        return -1;
    }

    out[0] = '\0';
    size_t offset = 0;

    if (!appendf(out, out_size, &offset,
                 "{\"uptime_ms\":%llu,\"reset_reason\":%d,"
                 "\"heap\":{\"free\":%u,\"minimum\":%u},"
                 "\"event_log\":{\"retained\":%u,\"written\":%u,\"dropped_oldest\":%u},"
                 "\"ap\":{\"started\":%s,\"ip\":",
                 (unsigned long long)snapshot->uptime_ms,
                 snapshot->reset_reason,
                 (unsigned)snapshot->heap_free,
                 (unsigned)snapshot->heap_minimum,
                 (unsigned)snapshot->event_log.retained,
                 (unsigned)snapshot->event_log.written,
                 (unsigned)snapshot->event_log.dropped_oldest,
                 snapshot->ap_started ? "true" : "false")) {
        return -1;
    }

    if (!append_json_string(out, out_size, &offset, snapshot->ap_ip) ||
        !appendf(out, out_size, &offset,
                 ",\"clients\":%u},"
                 "\"usb\":{\"connected\":%s,\"rx_bytes\":%llu,\"tx_bytes\":%llu},"
                 "\"bridge\":{\"usb_rx_bytes\":%llu,\"usb_tx_bytes\":%llu,"
                 "\"dropped_usb_rx\":%llu,\"dropped_usb_tx\":%llu,\"subscribers\":%u},"
                 "\"events\":[",
                 (unsigned)snapshot->wifi_clients,
                 snapshot->usb_connected ? "true" : "false",
                 (unsigned long long)snapshot->usb_bytes_received,
                 (unsigned long long)snapshot->usb_bytes_sent,
                 (unsigned long long)snapshot->bridge_bytes_from_usb,
                 (unsigned long long)snapshot->bridge_bytes_to_usb,
                 (unsigned long long)snapshot->bridge_dropped_from_usb,
                 (unsigned long long)snapshot->bridge_dropped_to_usb,
                 (unsigned)snapshot->bridge_subscribers)) {
        return -1;
    }

    for (size_t i = 0; i < snapshot->event_count; ++i) {
        const event_log_entry_t *event = &snapshot->events[i];
        if (i > 0 && !appendf(out, out_size, &offset, ",")) {
            return -1;
        }
        if (!appendf(out, out_size, &offset,
                     "{\"sequence\":%u,\"timestamp_ms\":%llu,\"level\":\"%s\",\"message\":",
                     (unsigned)event->sequence,
                     (unsigned long long)event->timestamp_ms,
                     level_name(event->level)) ||
            !append_json_string(out, out_size, &offset, event_message_for_export(event->message)) ||
            !appendf(out, out_size, &offset, "}")) {
            return -1;
        }
    }

    if (!appendf(out, out_size, &offset, "]}")) {
        return -1;
    }

    return (int)offset;
}
