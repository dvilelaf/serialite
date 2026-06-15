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

static void check_error_handler_does_not_recurse_via_send_err(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    const char *handler = strstr(web_server, "static esp_err_t http_error_handler");
    CHECK(handler != NULL);
    const char *next_function = strstr(handler + 1, "\nesp_err_t web_server_start");
    CHECK(next_function != NULL);

    const char *recursive_error_path = strstr(handler, "send_route_policy_error");
    CHECK(recursive_error_path == NULL || recursive_error_path > next_function);
    free(web_server);
}

static void check_login_screen_removed_from_primary_flow(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    CHECK(strstr(web_server, "static esp_err_t login_get_handler") == NULL);
    CHECK(strstr(web_server, "static esp_err_t login_post_handler") == NULL);
    CHECK(strstr(web_server, "Open console") == NULL);
    CHECK(strstr(web_server, "KVM Login") == NULL);
    free(web_server);
}

static void check_terminal_is_terminal_first_not_command_composer(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    CHECK(strstr(web_server, "<input id=\\\"input\\\"") == NULL);
    CHECK(strstr(web_server, "id=\\\"send\\\"") == NULL);
    CHECK(strstr(web_server, "sendInput()") == NULL);
    CHECK(strstr(web_server, "id=\\\"bar\\\"") == NULL);
    CHECK(strstr(web_server, "id=\\\"hud\\\"") != NULL);
    CHECK(strstr(web_server, "id=\\\"streamDot\\\"") != NULL);
    CHECK(strstr(web_server, "id=\\\"controlSwitch\\\"") == NULL);
    CHECK(strstr(web_server, "id=\\\"mode\\\"") == NULL);
    CHECK(strstr(web_server, "id=\\\"release\\\"") == NULL);
    CHECK(strstr(web_server, "id=\\\"control\\\"") == NULL);
    CHECK(strstr(web_server, "controlBtn.onclick") == NULL);
    CHECK(strstr(web_server, "/api/write/acquire") == NULL);
    CHECK(strstr(web_server, "/api/write/release") == NULL);
    CHECK(strstr(web_server, "Control active") == NULL);
    CHECK(strstr(web_server, "Press Enter to wake console") == NULL);
    CHECK(strstr(web_server, "Serial console ready") != NULL);
    CHECK(strstr(web_server, "/assets/xterm.js") != NULL);
    CHECK(strstr(web_server, "/assets/xterm.css") != NULL);
    CHECK(strstr(web_server, "new KvmTerminal.Terminal") != NULL);
    CHECK(strstr(web_server, "new KvmTerminal.FitAddon") != NULL);
    CHECK(strstr(web_server, "term.onData(data=>send(data))") != NULL);
    CHECK(strstr(web_server, "term.write") != NULL);
    CHECK(strstr(web_server, "attachCustomKeyEventHandler") != NULL);
    CHECK(strstr(web_server, "addEventListener('keydown',captureTerminalShortcuts,true)") != NULL);
    CHECK(strstr(web_server, "e.preventDefault();e.stopPropagation();send('\\\\u000c')") != NULL);
    CHECK(strstr(web_server, "send('\\\\u000c')") != NULL);
    CHECK(strstr(web_server, "streamText.textContent=ok?'OK':'ERROR'") != NULL);
    CHECK(strstr(web_server, "state.classList.toggle('expanded')") != NULL);
    CHECK(strstr(web_server, "ws.send(data)") != NULL);
    free(web_server);
}

static void check_terminal_output_uses_xterm_renderer(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    CHECK(strstr(web_server, "function appendTerminalData(t)") == NULL);
    CHECK(strstr(web_server, "terminal.textContent") == NULL);
    CHECK(strstr(web_server, "new Uint8Array(e.data)") != NULL);
    free(web_server);
}

static void check_diagnostics_has_clear_terminal_return(void)
{
    char *web_server = read_repo_source_file("components/web_server/src/web_server.c");
    const char *diagnostics = strstr(web_server, "static esp_err_t diagnostics_handler");
    CHECK(diagnostics != NULL);
    const char *next_function = strstr(diagnostics + 1, "static esp_err_t diagnostics_json_handler");
    CHECK(next_function != NULL);
    const char *back = strstr(diagnostics, "Back to terminal");
    CHECK(back != NULL && back < next_function);
    CHECK(strstr(back, "href=\\\"/terminal\\\"") != NULL && strstr(back, "href=\\\"/terminal\\\"") < next_function);
    free(web_server);
}

static void check_usb_serial_jtag_is_not_secondary_console(void)
{
    char *defaults = read_repo_source_file("sdkconfig.defaults");
    CHECK(strstr(defaults, "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y") == NULL);
    CHECK(strstr(defaults, "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y") != NULL);
    free(defaults);
}

static void check_lvgl_access_secrets_are_secrets_only(void)
{
    char *lvgl_ui = read_repo_source_file("components/lvgl_ui/src/lvgl_ui.c");
    const char *secrets = strstr(lvgl_ui, "lv_obj_t *secrets = add_card");
    CHECK(secrets != NULL);
    const char *end = strstr(secrets, "strlcpy(s_ctx.wifi_ssid");
    CHECK(end != NULL);

    CHECK(strstr(secrets, "WiFi password") != NULL && strstr(secrets, "WiFi password") < end);
    CHECK(strstr(secrets, "Web password") == NULL || strstr(secrets, "Web password") > end);
    CHECK(strstr(secrets, "lv_qrcode_create(secrets)") != NULL && strstr(secrets, "lv_qrcode_create(secrets)") < end);
    CHECK(strstr(secrets, "display_url") == NULL || strstr(secrets, "display_url") > end);
    CHECK(strstr(secrets, "http://") == NULL || strstr(secrets, "http://") > end);
    CHECK(strstr(secrets, "OPEN") == NULL || strstr(secrets, "OPEN") > end);
    free(lvgl_ui);
}

static void check_lvgl_access_secret_layout_avoids_overlap(void)
{
    char *lvgl_ui = read_repo_source_file("components/lvgl_ui/src/lvgl_ui.c");
    const char *secrets = strstr(lvgl_ui, "lv_obj_t *secrets = add_card");
    CHECK(secrets != NULL);
    const char *end = strstr(secrets, "strlcpy(s_ctx.wifi_ssid");
    CHECK(end != NULL);

    const char *hint_pos = strstr(secrets, "lv_obj_set_pos(s_ctx.secret_hint_label");
    CHECK(hint_pos != NULL && hint_pos < end);
    CHECK(strstr(hint_pos, "UI_SECRET_HINT_X, 0") != NULL);

    CHECK(strstr(lvgl_ui, "#define UI_SECRET_TEXT_X (UI_SECRET_RIGHT_X - 16)") != NULL);
    CHECK(strstr(lvgl_ui, "#define UI_SECRET_QR_Y ((UI_BOTTOM_CONTENT_H - UI_SECRET_QR_SIZE) / 2)") != NULL);
    CHECK(strstr(lvgl_ui, "#define UI_SECRET_TEXT_Y (((UI_BOTTOM_CONTENT_H - 54) / 2) - 8)") != NULL);
    CHECK(strstr(lvgl_ui, "#define UI_SECRET_HINT_X (UI_BOTTOM_CONTENT_W - 88)") != NULL);
    CHECK(strstr(secrets, "lv_obj_set_pos(s_ctx.wifi_placeholder, UI_CARD_PAD, UI_SECRET_QR_Y)") != NULL);
    CHECK(strstr(secrets, "lv_obj_set_pos(s_ctx.wifi_qr, UI_CARD_PAD, UI_SECRET_QR_Y)") != NULL);
    CHECK(strstr(secrets, "lv_obj_set_pos(wifi_title, UI_SECRET_TEXT_X, UI_SECRET_TEXT_Y)") != NULL);
    CHECK(strstr(secrets, "lv_obj_set_pos(s_ctx.wifi_password_label, UI_SECRET_TEXT_X, UI_SECRET_TEXT_Y + 22)") != NULL);
    CHECK(strstr(secrets, "web_title") == NULL || strstr(secrets, "web_title") > end);
    CHECK(strstr(secrets, "s_ctx.web_password_label") == NULL || strstr(secrets, "s_ctx.web_password_label") > end);
    free(lvgl_ui);
}

int main(void)
{
    CHECK(web_terminal_contract_has_required_statuses());
    CHECK(web_terminal_contract_has_mobile_keys());
    CHECK(strcmp(WEB_TERMINAL_STATUS_READ_ONLY, "SESSION") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITE_ACTIVE, "CONTROL") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITER_BUSY, "REPLACED") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_USB_DISCONNECTED, "USB OFF") == 0);
    check_pair_code_removed_from_normal_login();
    check_error_handler_does_not_recurse_via_send_err();
    check_login_screen_removed_from_primary_flow();
    check_terminal_is_terminal_first_not_command_composer();
    check_terminal_output_uses_xterm_renderer();
    check_diagnostics_has_clear_terminal_return();
    check_usb_serial_jtag_is_not_secondary_console();
    check_lvgl_access_secrets_are_secrets_only();
    check_lvgl_access_secret_layout_avoids_overlap();
    return 0;
}
