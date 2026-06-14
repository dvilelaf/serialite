#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_terminal_contract.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static char *read_source_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    CHECK(fseek(file, 0, SEEK_END) == 0);
    const long size = ftell(file);
    CHECK(size >= 0);
    CHECK(fseek(file, 0, SEEK_SET) == 0);

    char *contents = malloc((size_t)size + 1);
    CHECK(contents != NULL);
    CHECK(fread(contents, 1, (size_t)size, file) == (size_t)size);
    contents[size] = '\0';
    fclose(file);
    return contents;
}

static char *read_repo_source_file(const char *repo_path)
{
    const char *prefixes[] = {
        "",
        "../",
        "../../",
        "../../../",
    };
    char path[256];
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        snprintf(path, sizeof(path), "%s%s", prefixes[i], repo_path);
        char *contents = read_source_file(path);
        if (contents != NULL) {
            return contents;
        }
    }

    fprintf(stderr, "Unable to find source file: %s\n", repo_path);
    exit(1);
}

static void check_pair_code_removed_from_normal_login(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    CHECK(strstr(web_server, "First-login pair code") == NULL);
    CHECK(strstr(web_server, "name=\\\"pair\\\"") == NULL);
    CHECK(strstr(web_server, "login rejected: missing pair code") == NULL);
    CHECK(strstr(web_server, "local_pairing_verify_and_consume(&s_pairing, pair_code)") == NULL);
    free(web_server);

    char *lvgl_ui = read_repo_source_file("components/lvgl_ui/src/lvgl_ui.c");
    CHECK(strstr(lvgl_ui, "add_value(secrets, \"Pair\"") == NULL);
    free(lvgl_ui);
}

int main(void)
{
    CHECK(web_terminal_contract_has_required_statuses());
    CHECK(web_terminal_contract_has_mobile_keys());
    CHECK(strcmp(WEB_TERMINAL_STATUS_READ_ONLY, "Input locked") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITE_ACTIVE, "Input enabled") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITER_BUSY, "Input busy") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_USB_DISCONNECTED, "USB lost") == 0);
    CHECK(strcmp(WEB_TERMINAL_ACTION_UNLOCK, "Unlock input") == 0);
    CHECK(strcmp(WEB_TERMINAL_ACTION_LOCK, "Lock input") == 0);
    check_pair_code_removed_from_normal_login();
    return 0;
}
