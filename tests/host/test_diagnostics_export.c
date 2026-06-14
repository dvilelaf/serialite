#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostics_export.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_writes_json_without_secrets_or_invalid_strings(void)
{
    const event_log_entry_t events[] = {
        {
            .timestamp_ms = 10,
            .sequence = 1,
            .level = EVENT_LOG_SECURITY,
            .message = "login \"failed\"\nfrom web",
        },
        {
            .timestamp_ms = 20,
            .sequence = 2,
            .level = EVENT_LOG_WARN,
            .message = "queue full",
        },
        {
            .timestamp_ms = 30,
            .sequence = 3,
            .level = EVENT_LOG_SECURITY,
            .message = "password=hunter2 token=abcdef",
        },
    };
    const diagnostics_export_snapshot_t snapshot = {
        .uptime_ms = 1234,
        .reset_reason = 3,
        .heap_free = 45678,
        .heap_minimum = 32100,
        .event_log = {
            .written = 12,
            .dropped_oldest = 4,
            .retained = 8,
        },
        .ap_started = true,
        .ap_ip = "192.168.4.1",
        .wifi_clients = 2,
        .usb_connected = true,
        .usb_bytes_received = 100,
        .usb_bytes_sent = 200,
        .bridge_bytes_from_usb = 300,
        .bridge_bytes_to_usb = 400,
        .bridge_dropped_from_usb = 5,
        .bridge_dropped_to_usb = 6,
        .bridge_subscribers = 1,
        .events = events,
        .event_count = 3,
    };

    char json[2048];
    const int written = diagnostics_export_write_json(json, sizeof(json), &snapshot);

    CHECK(written > 0);
    CHECK((size_t)written == strlen(json));
    CHECK(strstr(json, "\"uptime_ms\":1234") != NULL);
    CHECK(strstr(json, "\"ap\":{\"started\":true,\"ip\":\"192.168.4.1\",\"clients\":2}") != NULL);
    CHECK(strstr(json, "\"usb\":{\"connected\":true,\"rx_bytes\":100,\"tx_bytes\":200}") != NULL);
    CHECK(strstr(json, "login \\\"failed\\\"\\nfrom web") != NULL);
    CHECK(strstr(json, "[redacted sensitive event]") != NULL);
    CHECK(strstr(json, "hunter2") == NULL);
    CHECK(strstr(json, "abcdef") == NULL);
    CHECK(strstr(json, "password") == NULL);
    CHECK(strstr(json, "secret") == NULL);
}

static void test_reports_overflow_instead_of_truncating_json(void)
{
    const diagnostics_export_snapshot_t snapshot = {
        .ap_ip = "192.168.4.1",
    };
    char json[16];

    CHECK(diagnostics_export_write_json(json, sizeof(json), &snapshot) < 0);
}

static void test_rejects_missing_event_array_when_events_are_requested(void)
{
    const diagnostics_export_snapshot_t snapshot = {
        .ap_ip = "192.168.4.1",
        .events = NULL,
        .event_count = 1,
    };
    char json[1024];

    CHECK(diagnostics_export_write_json(json, sizeof(json), &snapshot) < 0);
}

static void test_html_escape_prevents_breaking_diagnostics_page(void)
{
    char html[128];

    CHECK(diagnostics_export_write_html_escaped(html, sizeof(html), "</pre><script>alert('x')</script> & \"") > 0);
    CHECK(strcmp(html, "&lt;/pre&gt;&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt; &amp; &quot;") == 0);
}

int main(void)
{
    test_writes_json_without_secrets_or_invalid_strings();
    test_reports_overflow_instead_of_truncating_json();
    test_rejects_missing_event_array_when_events_are_requested();
    test_html_escape_prevents_breaking_diagnostics_page();
    return 0;
}
