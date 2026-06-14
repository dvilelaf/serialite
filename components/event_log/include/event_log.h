#pragma once

#include <stddef.h>
#include <stdint.h>

#define EVENT_LOG_MESSAGE_MAX 96
#define EVENT_LOG_DEFAULT_CAPACITY 32

typedef enum {
    EVENT_LOG_INFO = 0,
    EVENT_LOG_WARN,
    EVENT_LOG_ERROR,
    EVENT_LOG_SECURITY,
} event_log_level_t;

typedef struct {
    uint64_t timestamp_ms;
    uint32_t sequence;
    event_log_level_t level;
    char message[EVENT_LOG_MESSAGE_MAX];
} event_log_entry_t;

typedef struct {
    uint32_t written;
    uint32_t dropped_oldest;
    uint32_t retained;
} event_log_status_t;

void event_log_init(void);
void event_log_append(event_log_level_t level, uint64_t timestamp_ms, const char *message);
size_t event_log_snapshot(event_log_entry_t *out_entries, size_t max_entries);
event_log_status_t event_log_get_status(void);
