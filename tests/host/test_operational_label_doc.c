#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    const long len = ftell(f);
    assert(len > 0);
    assert(fseek(f, 0, SEEK_SET) == 0);

    char *buf = calloc((size_t)len + 1, 1);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    return buf;
}

static void operational_label_contains_required_fields(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/docs/operational-label.md", REPO_ROOT);
    char *doc = read_file(path);

    assert(strstr(doc, "ESP32-KVM") != NULL);
    assert(strstr(doc, "Serial rescue console") != NULL);
    assert(strstr(doc, "http://192.168.4.1") != NULL);
    assert(strstr(doc, "SSID: KVM") != NULL);
    assert(strstr(doc, "PWR 3s") != NULL);
    assert(strstr(doc, "BOOT 10s") != NULL);
    assert(strstr(doc, "Privileged physical console") != NULL);
    assert(strstr(doc, "Do not expose to Internet") != NULL);
    assert(strstr(doc, "No HDMI KVM") != NULL);
    assert(strstr(doc, "No secrets") != NULL);

    free(doc);
}

int main(void)
{
    operational_label_contains_required_fields();
    return 0;
}
