#include "esp_app_desc.h"

#include <stdio.h>
#include <string.h>

static const esp_app_desc_t s_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = "harness",
    .project_name = "serialite",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = "linux-harness",
};

char app_elf_sha256_str[] = "linux-harness";

const esp_app_desc_t *esp_app_get_description(void)
{
    return &s_app_desc;
}

int esp_app_get_elf_sha256(char *dst, size_t size)
{
    if (dst == NULL || size == 0U) {
        return 0;
    }
    const int written = snprintf(dst, size, "%s", app_elf_sha256_str);
    return written < 0 ? 0 : written + 1;
}
