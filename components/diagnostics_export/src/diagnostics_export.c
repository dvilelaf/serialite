#include "diagnostics_export.h"

#include <stdbool.h>
#include <string.h>

#include "json_writer.h"

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

static bool contains_case_insensitive(const char *value, const char *needle)
{
    const size_t needle_len = strlen(needle);
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (matched < needle_len && cursor[matched] != '\0') {
            char c = cursor[matched];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            if (c != needle[matched]) {
                break;
            }
            matched++;
        }
        if (matched == needle_len) {
            return true;
        }
    }
    return false;
}

/*
 * Event messages are written by the firmware and must never carry secrets.
 * This is a second line of defence for an export that is meant to be pasted
 * into tickets.
 */
static const char *event_message_for_export(const char *message)
{
    static const char *const secret_markers[] = {
        "password", "passwd", "secret", "token=", "cookie=", "authorization",
    };
    if (message == NULL) {
        return "";
    }
    for (size_t i = 0; i < sizeof(secret_markers) / sizeof(secret_markers[0]); ++i) {
        if (contains_case_insensitive(message, secret_markers[i])) {
            return "[redacted sensitive event]";
        }
    }
    return message;
}

int diagnostics_export_write_json(char *out, size_t out_size, const diagnostics_export_snapshot_t *snapshot)
{
    if (out == NULL || out_size == 0 || snapshot == NULL || (snapshot->event_count > 0 && snapshot->events == NULL)) {
        return -1;
    }

    json_writer_t w;
    json_writer_init(&w, out, out_size);
    json_writer_raw(
        &w,
        "{\"uptime_ms\":%llu,\"reset_reason\":%d,\"heap\":{\"free\":%u,\"minimum\":%u},"
        "\"event_log\":{\"retained\":%u,\"written\":%u,\"dropped_oldest\":%u},\"ap\":{\"started\":",
        (unsigned long long)snapshot->uptime_ms,
        snapshot->reset_reason,
        (unsigned)snapshot->heap_free,
        (unsigned)snapshot->heap_minimum,
        (unsigned)snapshot->event_log.retained,
        (unsigned)snapshot->event_log.written,
        (unsigned)snapshot->event_log.dropped_oldest);
    json_writer_bool(&w, snapshot->ap_started);
    json_writer_raw(&w, ",\"ip\":");
    json_writer_string(&w, snapshot->ap_ip);
    json_writer_raw(&w, ",\"clients\":%u},\"usb\":{\"connected\":", (unsigned)snapshot->wifi_clients);
    json_writer_bool(&w, snapshot->usb_connected);
    json_writer_raw(
        &w,
        ",\"rx_bytes\":%llu,\"tx_bytes\":%llu},\"bridge\":{\"usb_rx_bytes\":%llu,\"usb_tx_bytes\":%llu,"
        "\"dropped_usb_rx\":%llu,\"dropped_usb_tx\":%llu,\"subscribers\":%u},\"events\":[",
        (unsigned long long)snapshot->usb_bytes_received,
        (unsigned long long)snapshot->usb_bytes_sent,
        (unsigned long long)snapshot->bridge_bytes_from_usb,
        (unsigned long long)snapshot->bridge_bytes_to_usb,
        (unsigned long long)snapshot->bridge_dropped_from_usb,
        (unsigned long long)snapshot->bridge_dropped_to_usb,
        (unsigned)snapshot->bridge_subscribers);

    for (size_t i = 0; i < snapshot->event_count; ++i) {
        const event_log_entry_t *event = &snapshot->events[i];
        json_writer_raw(
            &w,
            "%s{\"sequence\":%u,\"timestamp_ms\":%llu,\"level\":\"%s\",\"message\":",
            i > 0 ? "," : "",
            (unsigned)event->sequence,
            (unsigned long long)event->timestamp_ms,
            level_name(event->level));
        json_writer_string(&w, event_message_for_export(event->message));
        json_writer_raw(&w, "}");
    }
    json_writer_raw(&w, "]}");

    return json_writer_finish(&w) ? (int)w.len : -1;
}
