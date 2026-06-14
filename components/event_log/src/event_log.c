#include "event_log.h"

#include <stdbool.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#else
#include <pthread.h>
#endif

typedef struct {
    event_log_entry_t entries[EVENT_LOG_DEFAULT_CAPACITY];
    uint32_t next_sequence;
    uint32_t written;
    uint32_t dropped_oldest;
    size_t start;
    size_t count;
    bool initialized;
} event_log_state_t;

static event_log_state_t s_log;

#ifdef ESP_PLATFORM
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;

static void event_log_lock(void)
{
    portENTER_CRITICAL(&s_log_lock);
}

static void event_log_unlock(void)
{
    portEXIT_CRITICAL(&s_log_lock);
}
#else
static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void event_log_lock(void)
{
    pthread_mutex_lock(&s_log_lock);
}

static void event_log_unlock(void)
{
    pthread_mutex_unlock(&s_log_lock);
}
#endif

static void event_log_init_unlocked(void)
{
    memset(&s_log, 0, sizeof(s_log));
    s_log.initialized = true;
}

void event_log_init(void)
{
    event_log_lock();
    event_log_init_unlocked();
    event_log_unlock();
}

static void ensure_initialized_unlocked(void)
{
    if (!s_log.initialized) {
        event_log_init_unlocked();
    }
}

void event_log_append(event_log_level_t level, uint64_t timestamp_ms, const char *message)
{
    event_log_lock();
    ensure_initialized_unlocked();

    size_t index = 0;
    if (s_log.count < EVENT_LOG_DEFAULT_CAPACITY) {
        index = (s_log.start + s_log.count) % EVENT_LOG_DEFAULT_CAPACITY;
        s_log.count++;
    } else {
        index = s_log.start;
        s_log.start = (s_log.start + 1) % EVENT_LOG_DEFAULT_CAPACITY;
        s_log.dropped_oldest++;
    }

    event_log_entry_t *entry = &s_log.entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->timestamp_ms = timestamp_ms;
    entry->sequence = ++s_log.next_sequence;
    entry->level = level;
    if (message != NULL) {
        strncpy(entry->message, message, sizeof(entry->message) - 1);
    }
    s_log.written++;
    event_log_unlock();
}

size_t event_log_snapshot(event_log_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0) {
        return 0;
    }

    event_log_lock();
    ensure_initialized_unlocked();
    const size_t to_copy = s_log.count < max_entries ? s_log.count : max_entries;
    const size_t first = s_log.count - to_copy;
    for (size_t i = 0; i < to_copy; ++i) {
        const size_t index = (s_log.start + first + i) % EVENT_LOG_DEFAULT_CAPACITY;
        out_entries[i] = s_log.entries[index];
    }
    event_log_unlock();
    return to_copy;
}

event_log_status_t event_log_get_status(void)
{
    event_log_lock();
    ensure_initialized_unlocked();
    const event_log_status_t status = {
        .written = s_log.written,
        .dropped_oldest = s_log.dropped_oldest,
        .retained = (uint32_t)s_log.count,
    };
    event_log_unlock();
    return status;
}
