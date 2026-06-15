#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static char *read_source_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    const long size = ftell(file);
    CHECK(size >= 0);
    CHECK(fseek(file, 0, SEEK_SET) == 0);

    char *contents = malloc((size_t)size + 1U);
    CHECK(contents != NULL);
    CHECK(fread(contents, 1, (size_t)size, file) == (size_t)size);
    contents[size] = '\0';
    fclose(file);
    return contents;
}

static char *read_repo_source_file(const char *repo_path)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", REPO_ROOT, repo_path);
    return read_source_file(path);
}

static void test_app_forces_single_wifi_client(void)
{
    char *app = read_repo_source_file("main/app_main.c");

    CHECK(strstr(app, "STRICT_WIFI_AP_MAX_CLIENTS 1") != NULL);
    CHECK(strstr(app, "apply_strict_runtime_wifi_policy(&mapped_wifi_config)") != NULL);
    CHECK(strstr(app, "apply_strict_stored_wifi_policy(&next.wifi)") != NULL);
    CHECK(strstr(app, "config->max_clients = STRICT_WIFI_AP_MAX_CLIENTS") != NULL);
    CHECK(strstr(app, "operational_config->max_clients = STRICT_WIFI_AP_MAX_CLIENTS") != NULL);
    CHECK(strstr(app, "config->max_clients = 4") == NULL);

    free(app);
}

static void test_emergency_lock_uses_pwr_not_boot(void)
{
    char *board_header = read_repo_source_file("components/board/include/board_waveshare_amoled.h");
    char *board = read_repo_source_file("components/board/src/board_waveshare_amoled.c");
    char *reset = read_repo_source_file("components/reset_control/src/reset_control.c");
    char *power_control = read_repo_source_file("components/power_control/src/power_control.c");

    CHECK(strstr(board_header, "board_waveshare_amoled_security_button_active") != NULL);
    CHECK(strstr(board, "BOARD_PIN_POWER_BUTTON IO_EXPANDER_PIN_NUM_4") != NULL);
    CHECK(strstr(board, "esp_io_expander_set_dir(s_io_expander, BOARD_PIN_POWER_BUTTON, IO_EXPANDER_INPUT)") != NULL);
    CHECK(strstr(board, "board_waveshare_amoled_security_button_active") != NULL);
    CHECK(strstr(board, "(levels & BOARD_PIN_POWER_BUTTON) != 0") != NULL);

    CHECK(strstr(reset, "lock_button_active = board_waveshare_amoled_security_button_active()") != NULL);
    CHECK(strstr(reset, "wake_button_active = board_waveshare_amoled_wake_button_active()") != NULL);
    CHECK(strstr(reset, "power_button_gesture_update(&power_gesture, lock_button_active") != NULL);
    CHECK(strstr(reset, "POWER_BUTTON_GESTURE_SHORT_RELEASE") != NULL);
    CHECK(strstr(reset, "POWER_BUTTON_GESTURE_LONG_HOLD") != NULL);
    CHECK(strstr(reset, "web_server_emergency_lock_toggle()") != NULL);
    CHECK(strstr(reset, "power_control_power_off()") != NULL);
    CHECK(strstr(reset, "web_server_emergency_lock()") == NULL);
    CHECK(strstr(reset, "reset_gesture_update(&reset_gesture, wake_button_active") != NULL);
    CHECK(strstr(reset, "emergency_lock_gesture_update(&lock_gesture, button_active") == NULL);
    CHECK(strstr(power_control, "AXP2101_POWER_OFF_REG 0x10") != NULL);
    CHECK(strstr(power_control, "AXP2101_POWER_OFF_BIT 0x01") != NULL);
    CHECK(strstr(power_control, "i2c_master_write_to_device") != NULL);

    free(board_header);
    free(board);
    free(reset);
    free(power_control);
}

int main(void)
{
    test_app_forces_single_wifi_client();
    test_emergency_lock_uses_pwr_not_boot();
    return 0;
}
