#include "lvgl_ui.h"

#include "app_watchdog.h"
#include "board_waveshare_amoled.h"
#include "power_status.h"
#include "secret_display.h"
#include "terminal_bridge.h"
#include "ui_status_format.h"
#include "usb_console.h"
#include "web_server.h"
#include "wifi_ap.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "draw/sw/lv_draw_sw_utils.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lvgl_ui";

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUF_LINES 20
#define LVGL_TASK_MIN_DELAY_MS 2
#define LVGL_TASK_MAX_DELAY_MS 50
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2
#define LVGL_DRAW_BUF_WIDTH ((BOARD_LCD_H_RES > BOARD_LCD_V_RES) ? BOARD_LCD_H_RES : BOARD_LCD_V_RES)
#define UI_LANDSCAPE_W BOARD_LCD_V_RES
#define UI_LANDSCAPE_H BOARD_LCD_H_RES
#define UI_SCREEN_PAD 18
#define UI_GAP 8
#define UI_TOP_H 28
#define UI_MAIN_Y (UI_SCREEN_PAD + UI_TOP_H + UI_GAP)
#define UI_MAIN_H 144
#define UI_ACCESS_W 170
#define UI_STATUS_W (UI_LANDSCAPE_W - (UI_SCREEN_PAD * 2) - UI_ACCESS_W - UI_GAP)
#define UI_BOTTOM_Y (UI_MAIN_Y + UI_MAIN_H + UI_GAP)
#define UI_BOTTOM_H (UI_LANDSCAPE_H - UI_BOTTOM_Y - UI_SCREEN_PAD)
#define UI_CARD_PAD 12
#define UI_ACCESS_CONTENT_W (UI_ACCESS_W - (UI_CARD_PAD * 2))
#define UI_STATUS_CONTENT_W (UI_STATUS_W - (UI_CARD_PAD * 2))
#define UI_BOTTOM_CONTENT_W (UI_LANDSCAPE_W - (UI_SCREEN_PAD * 2) - (UI_CARD_PAD * 2))
#define DISPLAY_IDLE_TIMEOUT_US (180LL * 1000LL * 1000LL)
#define WAKE_BUTTON_DEBOUNCE_US (50LL * 1000LL)
#define BATTERY_UPDATE_PERIOD_MS 10000
#define STATUS_UPDATE_PERIOD_MS 2000
#define SECRET_REVEAL_TIMEOUT_US (30LL * 1000LL * 1000LL)

#define UI_COLOR_BG 0x000000
#define UI_COLOR_BG_2 0x03110d
#define UI_COLOR_SURFACE 0x030807
#define UI_COLOR_SURFACE_2 0x071b16
#define UI_COLOR_LINE 0x12362d
#define UI_COLOR_TEXT 0xf2fff9
#define UI_COLOR_MUTED 0x6b8f85
#define UI_COLOR_OK 0x7dffe1
#define UI_COLOR_WARN 0xffd37a
#define UI_COLOR_BAD 0xff875c

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    lv_obj_t *battery_label;
    lv_obj_t *wifi_password_label;
    lv_obj_t *web_password_label;
    lv_obj_t *ssid_label;
    lv_obj_t *secret_hint_label;
    lv_obj_t *usb_status_label;
    lv_obj_t *client_status_label;
    lv_obj_t *audit_status_label;
    lv_obj_t *error_status_label;
    char wifi_password[96];
    char web_password[96];
    uint16_t *rotate_buf;
    SemaphoreHandle_t mutex;
    int64_t last_activity_us;
    int64_t secret_reveal_until_us;
    int64_t button_raw_changed_us;
    bool button_raw_pressed;
    bool button_pressed;
    bool secrets_visible;
    bool display_on;
    bool running;
} lvgl_ui_context_t;

static lvgl_ui_context_t s_ctx;

static void update_battery_label(lv_timer_t *timer)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (label == NULL || !s_ctx.display_on) {
        return;
    }

    power_status_t status = {0};
    const esp_err_t ret = power_status_read(&status);

    char text[24];
    if (ret == ESP_OK && status.available) {
        if (status.usb_connected && status.charging) {
            snprintf(text, sizeof(text), LV_SYMBOL_CHARGE " %d%%", status.percent);
        } else if (status.usb_connected) {
            snprintf(text, sizeof(text), LV_SYMBOL_USB " %d%%", status.percent);
        } else {
            snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_FULL " %d%%", status.percent);
        }
    } else {
        snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_FULL " --%%");
    }

    lv_label_set_text(label, text);
}

static void update_status_labels(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ctx.display_on ||
        s_ctx.usb_status_label == NULL ||
        s_ctx.client_status_label == NULL ||
        s_ctx.audit_status_label == NULL ||
        s_ctx.error_status_label == NULL) {
        return;
    }

    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const usb_console_status_t usb = usb_console_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    const web_server_status_t web = web_server_get_status();
    ui_status_format_output_t output;
    const ui_status_format_input_t input = {
        .ap_started = wifi.started,
        .wifi_clients = wifi.connected_clients,
        .web_clients = web.ws_client_count,
        .web_writer_active = web.writer_active,
        .web_locked = web.locked,
        .usb_connected = usb.connected,
        .usb_rx_bytes = usb.bytes_received,
        .usb_tx_bytes = usb.bytes_sent,
        .bridge_drops = bridge.dropped_from_usb + bridge.dropped_to_usb + bridge.scrollback_dropped_oldest,
    };

    if (!ui_status_format(&input, &output)) {
        return;
    }

    lv_label_set_text(s_ctx.usb_status_label, output.usb_line);
    lv_label_set_text(s_ctx.client_status_label, output.client_line);
    lv_label_set_text(s_ctx.audit_status_label, output.audit_line);
    lv_label_set_text(s_ctx.error_status_label, output.error_line);
    lv_obj_set_style_text_color(
        s_ctx.usb_status_label,
        usb.connected ? lv_color_hex(UI_COLOR_OK) : lv_color_hex(UI_COLOR_BAD),
        0);
    lv_obj_set_style_text_color(
        s_ctx.audit_status_label,
        web.locked ? lv_color_hex(UI_COLOR_BAD) : (web.writer_active ? lv_color_hex(UI_COLOR_WARN) : lv_color_hex(UI_COLOR_OK)),
        0);
    lv_obj_set_style_text_color(
        s_ctx.error_status_label,
        input.bridge_drops == 0 ? lv_color_hex(UI_COLOR_MUTED) : lv_color_hex(UI_COLOR_WARN),
        0);
}

static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata,
                               void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);
    return false;
}

static esp_err_t draw_rotated_bitmap(lv_display_t *display, esp_lcd_panel_handle_t panel, const lv_area_t *area, uint8_t *px_map)
{
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    if (rotation == LV_DISPLAY_ROTATION_0) {
        return esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }

    if (s_ctx.rotate_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const int32_t src_w = lv_area_get_width(area);
    const int32_t src_h = lv_area_get_height(area);
    const uint16_t *src = (const uint16_t *)px_map;
    uint16_t *dst = s_ctx.rotate_buf;
    lv_area_t rotated = {0};

    if (rotation == LV_DISPLAY_ROTATION_90) {
        rotated.x1 = area->y1;
        rotated.y1 = BOARD_LCD_V_RES - area->x2 - 1;
        rotated.x2 = area->y2;
        rotated.y2 = BOARD_LCD_V_RES - area->x1 - 1;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[(src_w - 1 - x) * src_h + y] = src[y * src_w + x];
            }
        }
    } else if (rotation == LV_DISPLAY_ROTATION_270) {
        rotated.x1 = BOARD_LCD_H_RES - area->y2 - 1;
        rotated.y1 = area->x1;
        rotated.x2 = BOARD_LCD_H_RES - area->y1 - 1;
        rotated.y2 = area->x2;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[x * src_h + (src_h - 1 - y)] = src[y * src_w + x];
            }
        }
    } else {
        rotated.x1 = BOARD_LCD_H_RES - area->x2 - 1;
        rotated.y1 = BOARD_LCD_V_RES - area->y2 - 1;
        rotated.x2 = BOARD_LCD_H_RES - area->x1 - 1;
        rotated.y2 = BOARD_LCD_V_RES - area->y1 - 1;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[(src_h - 1 - y) * src_w + (src_w - 1 - x)] = src[y * src_w + x];
            }
        }
    }

    return esp_lcd_panel_draw_bitmap(panel, rotated.x1, rotated.y1, rotated.x2 + 1, rotated.y2 + 1, dst);
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(display);
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
    esp_err_t err = draw_rotated_bitmap(display, panel, area, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel draw failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static void rounder_cb(lv_event_t *event)
{
    lv_area_t *area = lv_event_get_invalidated_area(event);
    if (area == NULL) {
        return;
    }

    lv_display_t *display = (lv_display_t *)lv_event_get_user_data(event);
    const int32_t hor_res = lv_display_get_horizontal_resolution(display);
    const int32_t ver_res = lv_display_get_vertical_resolution(display);

    area->x1 = (area->x1 < 0) ? 0 : ((area->x1 >> 1) << 1);
    area->y1 = (area->y1 < 0) ? 0 : ((area->y1 >> 1) << 1);
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
    if (area->x2 >= hor_res) {
        area->x2 = hor_res - 1;
    }
    if (area->y2 >= ver_res) {
        area->y2 = ver_res - 1;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool lock_lvgl(int timeout_ms)
{
    const TickType_t timeout_ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_ctx.mutex, timeout_ticks) == pdTRUE;
}

static void unlock_lvgl(void)
{
    xSemaphoreGiveRecursive(s_ctx.mutex);
}

static void set_secret_labels(bool reveal)
{
    char text[96];
    if (s_ctx.wifi_password_label != NULL &&
        secret_display_text(s_ctx.wifi_password, reveal, text, sizeof(text))) {
        lv_label_set_text(s_ctx.wifi_password_label, text);
    }
    if (s_ctx.web_password_label != NULL &&
        secret_display_text(s_ctx.web_password, reveal, text, sizeof(text))) {
        lv_label_set_text(s_ctx.web_password_label, text);
    }
    if (s_ctx.secret_hint_label != NULL) {
        if (reveal && secret_display_reveal_hint(SECRET_REVEAL_TIMEOUT_US / 1000ULL, text, sizeof(text))) {
            lv_label_set_text(s_ctx.secret_hint_label, text);
        } else {
            lv_label_set_text(s_ctx.secret_hint_label, "Press BOOT to reveal");
        }
        lv_obj_set_style_text_color(
            s_ctx.secret_hint_label,
            reveal ? lv_color_hex(UI_COLOR_WARN) : lv_color_hex(UI_COLOR_MUTED),
            0);
    }
    s_ctx.secrets_visible = reveal;
}

static void update_secret_countdown(int64_t now_us)
{
    if (s_ctx.secret_hint_label == NULL || !s_ctx.secrets_visible) {
        return;
    }

    const int64_t remaining_us = s_ctx.secret_reveal_until_us - now_us;
    if (remaining_us <= 0) {
        return;
    }

    char text[24];
    if (secret_display_reveal_hint((uint64_t)remaining_us / 1000ULL, text, sizeof(text))) {
        lv_label_set_text(s_ctx.secret_hint_label, text);
    }
}

static void reveal_secrets_temporarily(int64_t now_us)
{
    s_ctx.secret_reveal_until_us = now_us + SECRET_REVEAL_TIMEOUT_US;
    set_secret_labels(true);
}

static void handle_display_power(void)
{
    const int64_t now_us = esp_timer_get_time();
    const bool raw_pressed = board_waveshare_amoled_wake_button_active();

    if (raw_pressed != s_ctx.button_raw_pressed) {
        s_ctx.button_raw_pressed = raw_pressed;
        s_ctx.button_raw_changed_us = now_us;
    }

    if ((now_us - s_ctx.button_raw_changed_us) >= WAKE_BUTTON_DEBOUNCE_US &&
        raw_pressed != s_ctx.button_pressed) {
        s_ctx.button_pressed = raw_pressed;
        if (s_ctx.button_pressed) {
            const bool was_display_on = s_ctx.display_on;
            s_ctx.last_activity_us = now_us;
            if (!s_ctx.display_on) {
                board_waveshare_amoled_display_on();
                lv_obj_invalidate(lv_screen_active());
                s_ctx.display_on = true;
            }
            if (was_display_on) {
                reveal_secrets_temporarily(now_us);
            }
        }
    }

    if (s_ctx.secrets_visible) {
        if (now_us >= s_ctx.secret_reveal_until_us) {
            set_secret_labels(false);
        } else {
            update_secret_countdown(now_us);
        }
    }

    if (s_ctx.display_on && (now_us - s_ctx.last_activity_us) >= DISPLAY_IDLE_TIMEOUT_US) {
        set_secret_labels(false);
        board_waveshare_amoled_display_off();
        s_ctx.display_on = false;
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (lock_lvgl(-1)) {
            handle_display_power();
            delay_ms = lv_timer_handler();
            unlock_lvgl();
        }
        if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        } else if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int32_t width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *add_label_single_line(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int32_t width)
{
    lv_obj_t *label = add_label(parent, text, font, color, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static lv_obj_t *add_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(UI_COLOR_SURFACE_2), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_LINE), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, UI_CARD_PAD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *add_kicker(lv_obj_t *parent, const char *text, int32_t y, int32_t width)
{
    lv_obj_t *label = add_label(parent, text, &lv_font_montserrat_16, lv_color_hex(UI_COLOR_MUTED), width);
    lv_obj_set_pos(label, UI_CARD_PAD, y);
    return label;
}

static lv_obj_t *add_value(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int32_t y, int32_t width)
{
    lv_obj_t *label = add_label(parent, text, font, color, width);
    lv_obj_set_pos(label, UI_CARD_PAD, y);
    return label;
}

static void build_boot_screen(const lvgl_ui_boot_status_t *status)
{
    const char *ssid = status->ssid != NULL ? status->ssid : "(sin ssid)";
    const char *password = status->password != NULL ? status->password : "(sin password)";
    const char *web_password = status->web_password != NULL ? status->web_password : "(sin web password)";
    const char *https_fingerprint = status->https_fingerprint != NULL ? status->https_fingerprint : "";
    const char *web_url = status->web_url != NULL ? status->web_url : "";
    const char *ip_addr = status->ip_addr != NULL ? status->ip_addr : "192.168.4.1";
    char line[128];
    char url[64];
    char display_url[32];
    if (web_url[0] != '\0') {
        snprintf(url, sizeof(url), "%s", web_url);
    } else {
        snprintf(url, sizeof(url), "http://%s", ip_addr);
    }
    snprintf(display_url, sizeof(display_url), "%s", ip_addr);

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(UI_COLOR_BG_2), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *battery_label = lv_label_create(screen);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(UI_COLOR_OK), 0);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD, UI_SCREEN_PAD + 1);
    s_ctx.battery_label = battery_label;

    s_ctx.usb_status_label = add_label_single_line(
        screen,
        status->usb_connected ? "USB OK" : "USB LOST",
        &lv_font_montserrat_20,
        status->usb_connected ? lv_color_hex(UI_COLOR_OK) : lv_color_hex(UI_COLOR_BAD),
        150);
    lv_obj_set_pos(s_ctx.usb_status_label, UI_SCREEN_PAD, UI_SCREEN_PAD + 2);

    lv_obj_t *access = add_card(screen, UI_SCREEN_PAD, UI_MAIN_Y, UI_ACCESS_W, UI_MAIN_H);
    add_kicker(access, "AP", 0, UI_ACCESS_CONTENT_W);
    snprintf(line, sizeof(line), "%s", ssid);
    lv_obj_t *ssid_label = add_value(access, line, &lv_font_montserrat_28, lv_color_hex(UI_COLOR_OK), 22, UI_ACCESS_CONTENT_W);
    s_ctx.ssid_label = ssid_label;

    add_kicker(access, "OPEN", 66, UI_ACCESS_CONTENT_W);
    add_value(access, display_url, &lv_font_montserrat_28, lv_color_hex(UI_COLOR_TEXT), 88, UI_ACCESS_CONTENT_W);
    if (https_fingerprint[0] != '\0') {
        add_value(access, "HTTPS fingerprint on web", &lv_font_montserrat_16, lv_color_hex(UI_COLOR_WARN), 134, UI_ACCESS_CONTENT_W);
    }

    lv_obj_t *status_card = add_card(screen, UI_SCREEN_PAD + UI_ACCESS_W + UI_GAP, UI_MAIN_Y, UI_STATUS_W, UI_MAIN_H);
    add_kicker(status_card, "CLIENTS", 0, UI_STATUS_CONTENT_W);
    s_ctx.client_status_label = add_value(status_card, "0 WiFi  0 Web", &lv_font_montserrat_20, lv_color_hex(UI_COLOR_TEXT), 24, UI_STATUS_CONTENT_W);
    add_kicker(status_card, "INPUT", 64, UI_STATUS_CONTENT_W);
    s_ctx.audit_status_label = add_value(status_card, "Input idle", &lv_font_montserrat_20, lv_color_hex(UI_COLOR_OK), 88, UI_STATUS_CONTENT_W);
    s_ctx.error_status_label = add_value(status_card, "", &lv_font_montserrat_20, lv_color_hex(UI_COLOR_WARN), 116, UI_STATUS_CONTENT_W);

    lv_obj_t *secrets = add_card(screen, UI_SCREEN_PAD, UI_BOTTOM_Y, UI_LANDSCAPE_W - (UI_SCREEN_PAD * 2), UI_BOTTOM_H);
    s_ctx.secret_hint_label = add_value(secrets, "Press BOOT: reveal 30s", &lv_font_montserrat_16, lv_color_hex(UI_COLOR_MUTED), 0, UI_BOTTOM_CONTENT_W);

    add_value(secrets, "WiFi", &lv_font_montserrat_16, lv_color_hex(UI_COLOR_MUTED), 28, 44);
    s_ctx.wifi_password_label = add_label(secrets, password, &lv_font_montserrat_16, lv_color_hex(UI_COLOR_TEXT), UI_BOTTOM_CONTENT_W - 58);
    lv_obj_set_pos(s_ctx.wifi_password_label, 58, 28);

    add_value(secrets, "Web", &lv_font_montserrat_16, lv_color_hex(UI_COLOR_MUTED), 62, 44);
    s_ctx.web_password_label = add_label(secrets, web_password, &lv_font_montserrat_16, lv_color_hex(UI_COLOR_TEXT), UI_BOTTOM_CONTENT_W - 58);
    lv_obj_set_pos(s_ctx.web_password_label, 58, 62);

    strlcpy(s_ctx.wifi_password, password, sizeof(s_ctx.wifi_password));
    strlcpy(s_ctx.web_password, web_password, sizeof(s_ctx.web_password));
    set_secret_labels(false);

    lv_screen_load(screen);

    lv_timer_t *battery_timer = lv_timer_create(update_battery_label, BATTERY_UPDATE_PERIOD_MS, battery_label);
    if (battery_timer != NULL) {
        update_battery_label(battery_timer);
    }
    lv_timer_t *status_timer = lv_timer_create(update_status_labels, STATUS_UPDATE_PERIOD_MS, NULL);
    if (status_timer != NULL) {
        update_status_labels(status_timer);
    }
}

esp_err_t lvgl_ui_start(const lvgl_ui_boot_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is required");
    if (s_ctx.running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "initializing Waveshare AMOLED LVGL UI");
    lv_init();

    void *buf1 = heap_caps_malloc(
        LVGL_DRAW_BUF_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(
        LVGL_DRAW_BUF_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf1 != NULL && buf2 != NULL, ESP_ERR_NO_MEM, TAG, "lvgl draw buffer allocation failed");
    uint16_t *rotate_buf = heap_caps_malloc(
        LVGL_DRAW_BUF_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(rotate_buf != NULL, ESP_ERR_NO_MEM, TAG, "lvgl rotate buffer allocation failed");

    lv_display_t *display = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "lvgl display create failed");

    esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = notify_flush_ready,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(
        board_waveshare_amoled_new_display(&callbacks, display, &panel, NULL),
        TAG,
        "display init failed");

    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lv_display_set_buffers(
        display,
        buf1,
        buf2,
        LVGL_DRAW_BUF_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_add_event_cb(display, rounder_cb, LV_EVENT_INVALIDATE_AREA, display);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "tick timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "tick timer start failed");

    s_ctx.mutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_ctx.mutex != NULL, ESP_ERR_NO_MEM, TAG, "lvgl mutex create failed");
    s_ctx.display = display;
    s_ctx.panel = panel;
    s_ctx.rotate_buf = rotate_buf;
    s_ctx.display_on = true;
    s_ctx.button_raw_pressed = board_waveshare_amoled_wake_button_active();
    s_ctx.button_pressed = s_ctx.button_raw_pressed;
    s_ctx.button_raw_changed_us = esp_timer_get_time();
    s_ctx.last_activity_us = s_ctx.button_raw_changed_us;

    if (lock_lvgl(-1)) {
        build_boot_screen(status);
        unlock_lvgl();
    }

    BaseType_t task_ok = xTaskCreate(lvgl_task, "lvgl_ui", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "lvgl task create failed");

    board_waveshare_amoled_display_on();
    s_ctx.running = true;
    return ESP_OK;
}

esp_err_t lvgl_ui_update_credentials(const lvgl_ui_boot_status_t *status, bool reveal)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is required");
    ESP_RETURN_ON_FALSE(s_ctx.running && s_ctx.mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "lvgl is not running");

    if (!lock_lvgl(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    const char *ssid = status->ssid != NULL ? status->ssid : "";
    const char *password = status->password != NULL ? status->password : "";
    const char *web_password = status->web_password != NULL ? status->web_password : "";
    strlcpy(s_ctx.wifi_password, password, sizeof(s_ctx.wifi_password));
    strlcpy(s_ctx.web_password, web_password, sizeof(s_ctx.web_password));
    if (s_ctx.ssid_label != NULL && ssid[0] != '\0') {
        lv_label_set_text(s_ctx.ssid_label, ssid);
    }
    if (reveal) {
        reveal_secrets_temporarily(esp_timer_get_time());
    } else {
        set_secret_labels(false);
    }

    unlock_lvgl();
    return ESP_OK;
}
