#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_writer.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_builds_escaped_json(void)
{
    char buf[128];
    json_writer_t w;
    json_writer_init(&w, buf, sizeof(buf));
    json_writer_raw(&w, "{\"n\":%d,\"s\":", 7);
    json_writer_string(&w, "a\"b\\c\n\x01");
    json_writer_raw(&w, ",\"b\":");
    json_writer_bool(&w, true);
    json_writer_raw(&w, ",\"z\":");
    json_writer_string(&w, NULL);
    json_writer_raw(&w, "}");
    CHECK(json_writer_finish(&w));
    CHECK(strcmp(buf, "{\"n\":7,\"s\":\"a\\\"b\\\\c\\n\\u0001\",\"b\":true,\"z\":\"\"}") == 0);
    CHECK(w.len == strlen(buf));
}

static void test_overflow_is_sticky_and_terminated(void)
{
    char buf[8];
    json_writer_t w;
    json_writer_init(&w, buf, sizeof(buf));
    json_writer_raw(&w, "{\"a\":");
    json_writer_string(&w, "too long for the buffer");
    json_writer_raw(&w, "}");
    CHECK(!json_writer_finish(&w));
    CHECK(strlen(buf) < sizeof(buf));
}

int main(void)
{
    test_builds_escaped_json();
    test_overflow_is_sticky_and_terminated();
    return 0;
}
