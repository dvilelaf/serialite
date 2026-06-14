#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "event_log.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

#define THREAD_COUNT 8
#define EVENTS_PER_THREAD 10000

static void *append_many_events(void *arg)
{
    const size_t thread_id = (size_t)arg;
    char message[32];

    for (size_t i = 0; i < EVENTS_PER_THREAD; ++i) {
        snprintf(message, sizeof(message), "t%u-%u", (unsigned)thread_id, (unsigned)i);
        event_log_append(EVENT_LOG_INFO, i, message);
    }

    return NULL;
}

static void test_keeps_entries_in_order(void)
{
    event_log_init();
    event_log_append(EVENT_LOG_INFO, 10, "boot");
    event_log_append(EVENT_LOG_WARN, 20, "usb disconnected");

    event_log_entry_t entries[4];
    const size_t count = event_log_snapshot(entries, 4);

    CHECK(count == 2);
    CHECK(entries[0].sequence == 1);
    CHECK(entries[0].timestamp_ms == 10);
    CHECK(entries[0].level == EVENT_LOG_INFO);
    CHECK(strcmp(entries[0].message, "boot") == 0);
    CHECK(entries[1].sequence == 2);
    CHECK(strcmp(entries[1].message, "usb disconnected") == 0);
}

static void test_overwrites_oldest_when_full(void)
{
    event_log_init();
    char msg[16];
    for (size_t i = 0; i < EVENT_LOG_DEFAULT_CAPACITY + 3; ++i) {
        snprintf(msg, sizeof(msg), "event-%u", (unsigned)i);
        event_log_append(EVENT_LOG_INFO, i, msg);
    }

    event_log_entry_t entries[EVENT_LOG_DEFAULT_CAPACITY];
    const size_t count = event_log_snapshot(entries, EVENT_LOG_DEFAULT_CAPACITY);
    const event_log_status_t status = event_log_get_status();

    CHECK(count == EVENT_LOG_DEFAULT_CAPACITY);
    CHECK(status.written == EVENT_LOG_DEFAULT_CAPACITY + 3);
    CHECK(status.dropped_oldest == 3);
    CHECK(status.retained == EVENT_LOG_DEFAULT_CAPACITY);
    CHECK(entries[0].sequence == 4);
    CHECK(strcmp(entries[0].message, "event-3") == 0);
    CHECK(entries[count - 1].sequence == EVENT_LOG_DEFAULT_CAPACITY + 3);
}

static void test_bounded_snapshot_returns_latest_entries(void)
{
    event_log_init();
    char msg[16];
    for (size_t i = 0; i < EVENT_LOG_DEFAULT_CAPACITY; ++i) {
        snprintf(msg, sizeof(msg), "event-%u", (unsigned)i);
        event_log_append(EVENT_LOG_INFO, i, msg);
    }

    event_log_entry_t entries[4];
    const size_t count = event_log_snapshot(entries, 4);

    CHECK(count == 4);
    CHECK(entries[0].sequence == EVENT_LOG_DEFAULT_CAPACITY - 3);
    CHECK(entries[1].sequence == EVENT_LOG_DEFAULT_CAPACITY - 2);
    CHECK(entries[2].sequence == EVENT_LOG_DEFAULT_CAPACITY - 1);
    CHECK(entries[3].sequence == EVENT_LOG_DEFAULT_CAPACITY);
}

static void test_bounded_snapshot_returns_latest_entries_after_wrap(void)
{
    event_log_init();
    char msg[16];
    for (size_t i = 0; i < EVENT_LOG_DEFAULT_CAPACITY + 5; ++i) {
        snprintf(msg, sizeof(msg), "event-%u", (unsigned)i);
        event_log_append(EVENT_LOG_INFO, i, msg);
    }

    event_log_entry_t entries[4];
    const size_t count = event_log_snapshot(entries, 4);

    CHECK(count == 4);
    CHECK(entries[0].sequence == EVENT_LOG_DEFAULT_CAPACITY + 2);
    CHECK(entries[1].sequence == EVENT_LOG_DEFAULT_CAPACITY + 3);
    CHECK(entries[2].sequence == EVENT_LOG_DEFAULT_CAPACITY + 4);
    CHECK(entries[3].sequence == EVENT_LOG_DEFAULT_CAPACITY + 5);
}

static void test_truncates_long_messages(void)
{
    event_log_init();
    char long_message[EVENT_LOG_MESSAGE_MAX * 2];
    memset(long_message, 'A', sizeof(long_message));
    long_message[sizeof(long_message) - 1] = '\0';

    event_log_append(EVENT_LOG_SECURITY, 42, long_message);

    event_log_entry_t entry;
    CHECK(event_log_snapshot(&entry, 1) == 1);
    CHECK(strlen(entry.message) == EVENT_LOG_MESSAGE_MAX - 1);
    CHECK(entry.message[EVENT_LOG_MESSAGE_MAX - 1] == '\0');
}

static void test_concurrent_appends_keep_counters_consistent(void)
{
    event_log_init();

    pthread_t threads[THREAD_COUNT];
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        CHECK(pthread_create(&threads[i], NULL, append_many_events, (void *)i) == 0);
    }

    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    const event_log_status_t status = event_log_get_status();
    event_log_entry_t entries[EVENT_LOG_DEFAULT_CAPACITY];
    const size_t count = event_log_snapshot(entries, EVENT_LOG_DEFAULT_CAPACITY);

    CHECK(status.written == THREAD_COUNT * EVENTS_PER_THREAD);
    CHECK(status.retained == EVENT_LOG_DEFAULT_CAPACITY);
    CHECK(status.dropped_oldest == THREAD_COUNT * EVENTS_PER_THREAD - EVENT_LOG_DEFAULT_CAPACITY);
    CHECK(count == EVENT_LOG_DEFAULT_CAPACITY);
    CHECK(entries[0].sequence == status.written - EVENT_LOG_DEFAULT_CAPACITY + 1);
    CHECK(entries[count - 1].sequence == status.written);
}

int main(void)
{
    test_keeps_entries_in_order();
    test_overwrites_oldest_when_full();
    test_bounded_snapshot_returns_latest_entries();
    test_bounded_snapshot_returns_latest_entries_after_wrap();
    test_truncates_long_messages();
    test_concurrent_appends_keep_counters_consistent();
    return 0;
}
