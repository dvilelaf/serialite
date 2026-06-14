#include "lvgl_ui.h"

#include "app_watchdog.h"
#include "board_waveshare_amoled.h"
#include "power_status.h"
#include "secret_display.h"
#include "terminal_bridge.h"
#include "ui_status_format.h"
#include "usb_console.h"
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
#define UI_SCREEN_PAD 14
#define UI_LEFT_W 132
#define UI_GAP 14
#define UI_CARD_W (UI_LANDSCAPE_W - (UI_SCREEN_PAD * 2) - UI_LEFT_W - UI_GAP)
#define UI_CONTENT_W (UI_CARD_W - 28)
#define DISPLAY_IDLE_TIMEOUT_US (180LL * 1000LL * 1000LL)
#define WAKE_BUTTON_DEBOUNCE_US (50LL * 1000LL)
#define BATTERY_UPDATE_PERIOD_MS 10000
#define STATUS_UPDATE_PERIOD_MS 2000
#define SECRET_REVEAL_TIMEOUT_US (30LL * 1000LL * 1000LL)

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    lv_obj_t *battery_label;
    lv_obj_t *wifi_password_label;
    lv_obj_t *web_password_label;
    lv_obj_t *secret_hint_label;
    lv_obj_t *usb_status_label;
    lv_obj_t *client_status_label;
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
        s_ctx.error_status_label == NULL) {
        return;
    }

    const wifi_ap_status_t wifi = wifi_ap_get_status();
    const usb_console_status_t usb = usb_console_get_status();
    const terminal_bridge_status_t bridge = terminal_bridge_get_status();
    ui_status_format_output_t output;
    const ui_status_format_input_t input = {
        .ap_started = wifi.started,
        .wifi_clients = wifi.connected_clients,
        .web_clients = bridge.subscriber_count,
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
    lv_label_set_text(s_ctx.error_status_label, output.error_line);
    lv_obj_set_style_text_color(
        s_ctx.usb_status_label,
        usb.connected ? lv_color_hex(0x7dffe1) : lv_color_hex(0xff875c),
        0);
    lv_obj_set_style_text_color(
        s_ctx.error_status_label,
        input.bridge_drops == 0 ? lv_color_hex(0x6b8f85) : lv_color_hex(0xffd37a),
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
        lv_label_set_text(
            s_ctx.secret_hint_label,
            reveal ? "Secrets visible for 30s" : "BOOT reveal. Hold 3s lock web. Hold 10s reset.");
        lv_obj_set_style_text_color(
            s_ctx.secret_hint_label,
            reveal ? lv_color_hex(0xffd37a) : lv_color_hex(0x6b8f85),
            0);
    }
    s_ctx.secrets_visible = reveal;
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

    if (s_ctx.secrets_visible && now_us >= s_ctx.secret_reveal_until_us) {
        set_secret_labels(false);
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
    app_watchdog_register_current_task("lvgl_ui");
    while (true) {
        app_watchdog_reset_current_task();
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

static void build_boot_screen(const lvgl_ui_boot_status_t *status)
{
    const char *ssid = status->ssid != NULL ? status->ssid : "(sin ssid)";
    const char *password = status->password != NULL ? status->password : "(sin password)";
    const char *web_password = status->web_password != NULL ? status->web_password : "(sin web password)";
    const char *ip_addr = status->ip_addr != NULL ? status->ip_addr : "192.168.4.1";

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x04110d), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(screen, UI_SCREEN_PAD, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *battery_label = lv_label_create(screen);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x7dffe1), 0);
    lv_obj_align(battery_label, LV_ALIGN_TOP_LEFT, 0, -2);
    s_ctx.battery_label = battery_label;

    lv_obj_t *left = lv_obj_create(screen);
    lv_obj_set_size(left, UI_LEFT_W, UI_LANDSCAPE_H - (UI_SCREEN_PAD * 2));
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    add_label(left, "KVM", &lv_font_montserrat_28, lv_color_hex(0x7dffe1), UI_LEFT_W);
    add_label(left, "Rescue console", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_LEFT_W);

    lv_obj_t *ready_pill = lv_obj_create(left);
    lv_obj_set_width(ready_pill, 116);
    lv_obj_set_height(ready_pill, 38);
    lv_obj_set_style_margin_top(ready_pill, 24, 0);
    lv_obj_set_style_radius(ready_pill, 19, 0);
    lv_obj_set_style_bg_color(ready_pill, lv_color_hex(0x08291f), 0);
    lv_obj_set_style_border_color(ready_pill, lv_color_hex(0x2ee6b8), 0);
    lv_obj_set_style_border_width(ready_pill, 1, 0);
    lv_obj_set_style_pad_all(ready_pill, 0, 0);

    lv_obj_t *ready_label = lv_label_create(ready_pill);
    lv_label_set_text(ready_label, "AP READY");
    lv_obj_set_style_text_font(ready_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ready_label, lv_color_hex(0x7dffe1), 0);
    lv_obj_center(ready_label);

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, UI_CARD_W, UI_LANDSCAPE_H - (UI_SCREEN_PAD * 2));
    lv_obj_align(card, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x030807), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x12362d), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    char line[128];
    add_label(card, "1  Join WiFi", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    snprintf(line, sizeof(line), "%s", ssid);
    lv_obj_t *ssid_label = add_label(card, line, &lv_font_montserrat_28, lv_color_hex(0xffffff), UI_CONTENT_W);
    lv_obj_set_style_margin_bottom(ssid_label, 8, 0);

    add_label(card, "2  WiFi password", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    lv_obj_t *password_label = add_label(card, password, &lv_font_montserrat_16, lv_color_hex(0xffffff), UI_CONTENT_W);
    lv_obj_set_style_margin_bottom(password_label, 4, 0);
    s_ctx.wifi_password_label = password_label;

    add_label(card, "3  Web password", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    lv_obj_t *web_password_label = add_label(card, web_password, &lv_font_montserrat_16, lv_color_hex(0xffffff), UI_CONTENT_W);
    lv_obj_set_style_margin_bottom(web_password_label, 4, 0);
    s_ctx.web_password_label = web_password_label;

    add_label(card, "4  Open", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    snprintf(line, sizeof(line), "http://%s", ip_addr);
    add_label(card, line, &lv_font_montserrat_20, lv_color_hex(0x7dffe1), UI_CONTENT_W);

    add_label(card, "Status", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    s_ctx.usb_status_label = add_label(card, status->usb_connected ? "USB connected" : "USB disconnected", &lv_font_montserrat_16, lv_color_hex(0xff875c), UI_CONTENT_W);
    s_ctx.client_status_label = add_label(card, "AP starting  WiFi 0  Web 0", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
    s_ctx.error_status_label = add_label(card, "No bridge drops", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);

    s_ctx.secret_hint_label = add_label(card, "BOOT: reveal. Hold 3s: lock web. Hold 10s: reset.", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_CONTENT_W);
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
