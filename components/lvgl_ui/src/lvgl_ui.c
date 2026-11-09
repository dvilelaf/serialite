#include "lvgl_ui.h"

#include "board.h"
#include "board_display.h"
#include "credentials.h"
#include "secret_display.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "draw/sw/lv_draw_sw_utils.h"
#include "lvgl.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lvgl_ui";

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUF_LINES 20
#define LVGL_TASK_MIN_DELAY_MS 2
#define LVGL_TASK_MAX_DELAY_MS 50
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2

/* Layout tuned for the 448x368 landscape AMOLED; other panels need their own numbers. */
#define UI_SCREEN_PAD 18
#define UI_GAP 8
#define UI_TOP_H 28
#define UI_MAIN_Y (UI_SCREEN_PAD + UI_TOP_H + UI_GAP)
#define UI_MAIN_H 144
#define UI_ACCESS_W 170
#define UI_STATUS_W (s_ctx.screen_w - (UI_SCREEN_PAD * 2) - UI_ACCESS_W - UI_GAP)
#define UI_BOTTOM_Y (UI_MAIN_Y + UI_MAIN_H + UI_GAP)
#define UI_BOTTOM_H (s_ctx.screen_h - UI_BOTTOM_Y - UI_SCREEN_PAD)
#define UI_CARD_PAD 12
#define UI_SECRET_QR_SIZE 120
#define UI_BOTTOM_CONTENT_W (s_ctx.screen_w - (UI_SCREEN_PAD * 2) - (UI_CARD_PAD * 2))
#define UI_BOTTOM_CONTENT_H (UI_BOTTOM_H - (UI_CARD_PAD * 2))
#define UI_SECRET_QR_Y ((UI_BOTTOM_CONTENT_H - UI_SECRET_QR_SIZE) / 2)
#define UI_SECRET_TEXT_X 156
#define UI_SECRET_TEXT_W (UI_BOTTOM_CONTENT_W - UI_SECRET_TEXT_X)
#define UI_SECRET_TEXT_Y (((UI_BOTTOM_CONTENT_H - 54) / 2) - 8)
#define UI_SECRET_HINT_W 96
#define UI_SECRET_HINT_X (UI_BOTTOM_CONTENT_W - UI_SECRET_HINT_W + 8)
#define UI_ACCESS_CONTENT_W (UI_ACCESS_W - (UI_CARD_PAD * 2))
#define UI_STATUS_CONTENT_W (UI_STATUS_W - (UI_CARD_PAD * 2))

#define DISPLAY_IDLE_TIMEOUT_US (180LL * 1000LL * 1000LL)
#define SECRET_REVEAL_TIMEOUT_US (30LL * 1000LL * 1000LL)
#define RESET_COUNTDOWN_AFTER_MS 2000U
#define BATTERY_UPDATE_PERIOD_MS 10000
#define STATUS_UPDATE_PERIOD_MS 2000

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

#define UI_SSID_MAX 33
#define UI_PASSWORD_MAX 96
#define UI_HOST_MAX 40
#define UI_FINGERPRINT_MAX 100

typedef enum {
    SECRET_PAGE_HIDDEN = 0,
    SECRET_PAGE_WIFI,
    SECRET_PAGE_TLS,
} secret_page_t;

typedef struct {
    char ssid[UI_SSID_MAX];
    char password[UI_PASSWORD_MAX];
    char host[UI_HOST_MAX];
    char fingerprint[UI_FINGERPRINT_MAX];
} ui_credentials_copy_t;

/* Requests from other tasks, applied by the UI task. */
typedef struct {
    bool credentials_pending;
    bool reveal;
    ui_credentials_copy_t credentials;
    unsigned presses;
    bool hold_pending;
    uint32_t hold_ms;
    uint32_t reset_after_ms;
} ui_mailbox_t;

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    const board_display_info_t *display_info;
    int32_t screen_w;
    int32_t screen_h;
    lvgl_ui_status_fn_t status_fn;
    void *status_ctx;
    lv_obj_t *battery_label;
    lv_obj_t *usb_status_label;
    lv_obj_t *client_status_label;
    lv_obj_t *audit_status_label;
    lv_obj_t *error_status_label;
    lv_obj_t *ssid_label;
    lv_obj_t *host_label;
    lv_obj_t *tls_hint_label;
    lv_obj_t *wifi_qr;
    lv_obj_t *wifi_placeholder;
    lv_obj_t *secret_title_label;
    lv_obj_t *secret_value_label;
    lv_obj_t *secret_hint_label;
    ui_credentials_copy_t credentials;
    uint16_t *rotate_buf;
    int64_t last_activity_us;
    int64_t secret_reveal_until_us;
    secret_page_t secret_page;
    bool reset_countdown_visible;
    bool display_on;
} ui_context_t;

static ui_context_t s_ctx;
static ui_mailbox_t s_mailbox;
static SemaphoreHandle_t s_mailbox_lock;
static StaticSemaphore_t s_mailbox_lock_storage;
static bool s_running;

static void copy_credentials(ui_credentials_copy_t *dst, const lvgl_ui_credentials_t *src)
{
    memset(dst, 0, sizeof(*dst));
    strlcpy(dst->ssid, src->ssid != NULL ? src->ssid : "", sizeof(dst->ssid));
    strlcpy(dst->password, src->password != NULL ? src->password : "", sizeof(dst->password));
    strlcpy(dst->host, src->web_host != NULL ? src->web_host : "", sizeof(dst->host));
    strlcpy(dst->fingerprint, src->https_fingerprint != NULL ? src->https_fingerprint : "", sizeof(dst->fingerprint));
}

/* ---- status refresh ---------------------------------------------------- */

static void update_battery_label(lv_timer_t *timer)
{
    (void)timer;
    if (s_ctx.battery_label == NULL || !s_ctx.display_on) {
        return;
    }

    board_battery_t battery;
    char text[24];
    if (board_battery_read(&battery) == ESP_OK) {
        const char *symbol = battery.charging ? LV_SYMBOL_CHARGE : (battery.external_power ? LV_SYMBOL_USB : LV_SYMBOL_BATTERY_FULL);
        snprintf(text, sizeof(text), "%s %d%%", symbol, battery.percent);
    } else {
        snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_FULL " --%%");
    }
    lv_label_set_text(s_ctx.battery_label, text);
}

static void update_status_labels(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ctx.display_on || s_ctx.status_fn == NULL) {
        return;
    }

    ui_status_format_input_t input = {0};
    s_ctx.status_fn(&input, s_ctx.status_ctx);
    ui_status_format_output_t output;
    if (!ui_status_format(&input, &output)) {
        return;
    }

    lv_label_set_text(s_ctx.usb_status_label, output.usb_line);
    lv_label_set_text(s_ctx.client_status_label, output.client_line);
    lv_label_set_text(s_ctx.audit_status_label, output.audit_line);
    lv_label_set_text(s_ctx.error_status_label, output.error_line);
    lv_obj_set_style_text_color(s_ctx.usb_status_label, lv_color_hex(input.usb_connected ? UI_COLOR_OK : UI_COLOR_BAD), 0);
    lv_obj_set_style_text_color(
        s_ctx.audit_status_label,
        lv_color_hex(input.web_locked ? UI_COLOR_BAD : (input.web_writer_active ? UI_COLOR_WARN : UI_COLOR_OK)),
        0);
    lv_obj_set_style_text_color(s_ctx.error_status_label, lv_color_hex(input.bridge_drops == 0 ? UI_COLOR_MUTED : UI_COLOR_WARN), 0);
}

/* ---- display plumbing --------------------------------------------------- */

static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static esp_err_t draw_rotated_bitmap(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t panel_w = s_ctx.display_info->width;
    const int32_t panel_h = s_ctx.display_info->height;
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    if (rotation == LV_DISPLAY_ROTATION_0) {
        return esp_lcd_panel_draw_bitmap(s_ctx.panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }

    const int32_t src_w = lv_area_get_width(area);
    const int32_t src_h = lv_area_get_height(area);
    const uint16_t *src = (const uint16_t *)px_map;
    uint16_t *dst = s_ctx.rotate_buf;
    lv_area_t rotated = {0};

    if (rotation == LV_DISPLAY_ROTATION_90) {
        rotated.x1 = area->y1;
        rotated.y1 = panel_h - area->x2 - 1;
        rotated.x2 = area->y2;
        rotated.y2 = panel_h - area->x1 - 1;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[(src_w - 1 - x) * src_h + y] = src[y * src_w + x];
            }
        }
    } else if (rotation == LV_DISPLAY_ROTATION_270) {
        rotated.x1 = panel_w - area->y2 - 1;
        rotated.y1 = area->x1;
        rotated.x2 = panel_w - area->y1 - 1;
        rotated.y2 = area->x2;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[x * src_h + (src_h - 1 - y)] = src[y * src_w + x];
            }
        }
    } else {
        rotated.x1 = panel_w - area->x2 - 1;
        rotated.y1 = panel_h - area->y2 - 1;
        rotated.x2 = panel_w - area->x1 - 1;
        rotated.y2 = panel_h - area->y1 - 1;
        for (int32_t y = 0; y < src_h; y++) {
            for (int32_t x = 0; x < src_w; x++) {
                dst[(src_h - 1 - y) * src_w + (src_w - 1 - x)] = src[y * src_w + x];
            }
        }
    }

    return esp_lcd_panel_draw_bitmap(s_ctx.panel, rotated.x1, rotated.y1, rotated.x2 + 1, rotated.y2 + 1, dst);
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
    const esp_err_t err = draw_rotated_bitmap(display, area, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel draw failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static void even_area_rounder_cb(lv_event_t *event)
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

/* ---- secrets ------------------------------------------------------------ */

static bool has_fingerprint(void)
{
    return s_ctx.credentials.fingerprint[0] != '\0';
}

static void show_secret_hint(const char *text, uint32_t color)
{
    lv_label_set_text(s_ctx.secret_hint_label, text);
    lv_obj_set_style_text_color(s_ctx.secret_hint_label, lv_color_hex(color), 0);
    lv_obj_remove_flag(s_ctx.secret_hint_label, LV_OBJ_FLAG_HIDDEN);
}

static void hide_secret_hint(void)
{
    lv_label_set_text(s_ctx.secret_hint_label, "");
    lv_obj_add_flag(s_ctx.secret_hint_label, LV_OBJ_FLAG_HIDDEN);
}

static void set_secret_page(secret_page_t page)
{
    char text[UI_FINGERPRINT_MAX];
    const bool tls = page == SECRET_PAGE_TLS;
    const bool show_qr = page == SECRET_PAGE_WIFI;

    if (show_qr) {
        char payload[192];
        if (!credentials_wifi_qr_payload(s_ctx.credentials.ssid, s_ctx.credentials.password, payload, sizeof(payload))) {
            payload[0] = '\0';
        }
        lv_qrcode_set_data(s_ctx.wifi_qr, payload);
        mbedtls_platform_zeroize(payload, sizeof(payload));
        lv_obj_remove_flag(s_ctx.wifi_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ctx.wifi_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_qrcode_set_data(s_ctx.wifi_qr, "");
        lv_obj_add_flag(s_ctx.wifi_qr, LV_OBJ_FLAG_HIDDEN);
        if (tls) {
            lv_obj_add_flag(s_ctx.wifi_placeholder, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_ctx.wifi_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* The TLS page uses the full card width so the fingerprint fits in two or three lines. */
    const int32_t text_x = tls ? 0 : UI_SECRET_TEXT_X;
    const int32_t text_y = tls ? 0 : UI_SECRET_TEXT_Y;
    const int32_t text_w = tls ? UI_BOTTOM_CONTENT_W - UI_SECRET_HINT_W : UI_SECRET_TEXT_W;
    lv_obj_set_pos(s_ctx.secret_title_label, text_x, text_y);
    lv_obj_set_width(s_ctx.secret_title_label, text_w);
    lv_obj_set_pos(s_ctx.secret_value_label, text_x, text_y + 22);
    lv_obj_set_width(s_ctx.secret_value_label, tls ? UI_BOTTOM_CONTENT_W : UI_SECRET_TEXT_W);
    lv_label_set_text(s_ctx.secret_title_label, tls ? "HTTPS certificate SHA-256" : "WiFi password");

    if (tls) {
        lv_label_set_text(s_ctx.secret_value_label, s_ctx.credentials.fingerprint);
    } else if (secret_display_text(s_ctx.credentials.password, show_qr, text, sizeof(text))) {
        lv_label_set_text(s_ctx.secret_value_label, text);
    }
    mbedtls_platform_zeroize(text, sizeof(text));

    if (page == SECRET_PAGE_HIDDEN) {
        hide_secret_hint();
    }
    s_ctx.secret_page = page;
}

static void update_secret_countdown(int64_t now_us)
{
    if (s_ctx.secret_page == SECRET_PAGE_HIDDEN || s_ctx.reset_countdown_visible) {
        return;
    }
    const int64_t remaining_us = s_ctx.secret_reveal_until_us - now_us;
    char text[24];
    if (remaining_us > 0 && secret_display_reveal_hint((uint64_t)remaining_us / 1000ULL, text, sizeof(text))) {
        show_secret_hint(text, UI_COLOR_WARN);
    }
}

static void reveal_page(secret_page_t page, int64_t now_us)
{
    s_ctx.secret_reveal_until_us = now_us + SECRET_REVEAL_TIMEOUT_US;
    set_secret_page(page);
    update_secret_countdown(now_us);
}

static void set_display_power(bool on)
{
    if (on == s_ctx.display_on) {
        return;
    }
    if (on) {
        board_display_set_power(true);
        lv_obj_invalidate(lv_screen_active());
    } else {
        set_secret_page(SECRET_PAGE_HIDDEN);
        board_display_set_power(false);
    }
    s_ctx.display_on = on;
}

static void handle_press(int64_t now_us)
{
    s_ctx.last_activity_us = now_us;
    if (!s_ctx.display_on) {
        set_display_power(true);
        return;
    }
    switch (s_ctx.secret_page) {
        case SECRET_PAGE_HIDDEN:
            reveal_page(SECRET_PAGE_WIFI, now_us);
            break;
        case SECRET_PAGE_WIFI:
            reveal_page(has_fingerprint() ? SECRET_PAGE_TLS : SECRET_PAGE_WIFI, now_us);
            break;
        case SECRET_PAGE_TLS:
        default:
            reveal_page(SECRET_PAGE_WIFI, now_us);
            break;
    }
}

static void handle_hold(uint32_t held_ms, uint32_t reset_after_ms, int64_t now_us)
{
    if (held_ms >= RESET_COUNTDOWN_AFTER_MS && held_ms < reset_after_ms) {
        char text[24];
        snprintf(text, sizeof(text), "Reset in %us", (unsigned)((reset_after_ms - held_ms + 999U) / 1000U));
        s_ctx.last_activity_us = now_us;
        set_display_power(true);
        show_secret_hint(text, UI_COLOR_BAD);
        s_ctx.reset_countdown_visible = true;
        return;
    }
    if (held_ms == 0 && s_ctx.reset_countdown_visible) {
        s_ctx.reset_countdown_visible = false;
        if (s_ctx.secret_page == SECRET_PAGE_HIDDEN) {
            hide_secret_hint();
        } else {
            update_secret_countdown(now_us);
        }
    }
}

static void apply_credentials(const ui_credentials_copy_t *credentials, bool reveal, int64_t now_us)
{
    mbedtls_platform_zeroize(&s_ctx.credentials, sizeof(s_ctx.credentials));
    s_ctx.credentials = *credentials;
    lv_label_set_text(s_ctx.ssid_label, s_ctx.credentials.ssid);
    lv_label_set_text(s_ctx.host_label, s_ctx.credentials.host);
    if (has_fingerprint()) {
        lv_obj_remove_flag(s_ctx.tls_hint_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ctx.tls_hint_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (reveal) {
        s_ctx.last_activity_us = now_us;
        set_display_power(true);
        reveal_page(SECRET_PAGE_WIFI, now_us);
    } else {
        set_secret_page(SECRET_PAGE_HIDDEN);
    }
}

/* ---- UI task ------------------------------------------------------------ */

static void process_mailbox(int64_t now_us)
{
    ui_mailbox_t mail;
    xSemaphoreTake(s_mailbox_lock, portMAX_DELAY);
    mail = s_mailbox;
    mbedtls_platform_zeroize(&s_mailbox, sizeof(s_mailbox));
    xSemaphoreGive(s_mailbox_lock);

    if (mail.credentials_pending) {
        apply_credentials(&mail.credentials, mail.reveal, now_us);
    }
    for (unsigned i = 0; i < mail.presses; ++i) {
        handle_press(now_us);
    }
    if (mail.hold_pending) {
        handle_hold(mail.hold_ms, mail.reset_after_ms, now_us);
    }
    mbedtls_platform_zeroize(&mail, sizeof(mail));
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        const int64_t now_us = esp_timer_get_time();
        process_mailbox(now_us);

        if (s_ctx.secret_page != SECRET_PAGE_HIDDEN) {
            if (now_us >= s_ctx.secret_reveal_until_us) {
                set_secret_page(SECRET_PAGE_HIDDEN);
            } else {
                update_secret_countdown(now_us);
            }
        }
        if (s_ctx.display_on && (now_us - s_ctx.last_activity_us) >= DISPLAY_IDLE_TIMEOUT_US) {
            set_display_power(false);
        }

        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        } else if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ---- screen ------------------------------------------------------------- */

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int32_t width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *add_label_at(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int32_t y, int32_t width)
{
    lv_obj_t *label = add_label(parent, text, font, color, width);
    lv_obj_set_pos(label, UI_CARD_PAD, y);
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

static void build_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(UI_COLOR_BG_2), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ctx.battery_label = lv_label_create(screen);
    lv_label_set_text(s_ctx.battery_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(s_ctx.battery_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ctx.battery_label, lv_color_hex(UI_COLOR_OK), 0);
    lv_obj_align(s_ctx.battery_label, LV_ALIGN_TOP_RIGHT, -UI_SCREEN_PAD, UI_SCREEN_PAD + 1);
    if (!board_get_info()->has_battery) {
        lv_obj_add_flag(s_ctx.battery_label, LV_OBJ_FLAG_HIDDEN);
    }

    s_ctx.usb_status_label = add_label(screen, "USB LOST", &lv_font_montserrat_20, UI_COLOR_BAD, 150);
    lv_label_set_long_mode(s_ctx.usb_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_ctx.usb_status_label, UI_SCREEN_PAD, UI_SCREEN_PAD + 2);

    lv_obj_t *access = add_card(screen, UI_SCREEN_PAD, UI_MAIN_Y, UI_ACCESS_W, UI_MAIN_H);
    add_label_at(access, "AP", &lv_font_montserrat_16, UI_COLOR_MUTED, 0, UI_ACCESS_CONTENT_W);
    s_ctx.ssid_label = add_label_at(access, "", &lv_font_montserrat_28, UI_COLOR_OK, 22, UI_ACCESS_CONTENT_W);
    add_label_at(access, "OPEN", &lv_font_montserrat_16, UI_COLOR_MUTED, 66, UI_ACCESS_CONTENT_W);
    s_ctx.host_label = add_label_at(access, "", &lv_font_montserrat_28, UI_COLOR_TEXT, 88, UI_ACCESS_CONTENT_W);
    s_ctx.tls_hint_label = add_label_at(access, "HTTPS: BOOT x2", &lv_font_montserrat_16, UI_COLOR_WARN, 134, UI_ACCESS_CONTENT_W);
    lv_obj_add_flag(s_ctx.tls_hint_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *status_card = add_card(screen, UI_SCREEN_PAD + UI_ACCESS_W + UI_GAP, UI_MAIN_Y, UI_STATUS_W, UI_MAIN_H);
    add_label_at(status_card, "CLIENTS", &lv_font_montserrat_16, UI_COLOR_MUTED, 0, UI_STATUS_CONTENT_W);
    s_ctx.client_status_label = add_label_at(status_card, "0 WiFi  0 Web", &lv_font_montserrat_20, UI_COLOR_TEXT, 24, UI_STATUS_CONTENT_W);
    add_label_at(status_card, "INPUT", &lv_font_montserrat_16, UI_COLOR_MUTED, 64, UI_STATUS_CONTENT_W);
    s_ctx.audit_status_label = add_label_at(status_card, "Input idle", &lv_font_montserrat_20, UI_COLOR_OK, 88, UI_STATUS_CONTENT_W);
    s_ctx.error_status_label = add_label_at(status_card, "", &lv_font_montserrat_20, UI_COLOR_WARN, 116, UI_STATUS_CONTENT_W);

    lv_obj_t *secrets = add_card(screen, UI_SCREEN_PAD, UI_BOTTOM_Y, s_ctx.screen_w - (UI_SCREEN_PAD * 2), UI_BOTTOM_H);
    s_ctx.secret_hint_label = add_label(secrets, "", &lv_font_montserrat_16, UI_COLOR_MUTED, UI_SECRET_HINT_W);
    lv_obj_set_pos(s_ctx.secret_hint_label, UI_SECRET_HINT_X, 0);
    lv_obj_add_flag(s_ctx.secret_hint_label, LV_OBJ_FLAG_HIDDEN);

    s_ctx.wifi_placeholder = lv_obj_create(secrets);
    lv_obj_set_size(s_ctx.wifi_placeholder, UI_SECRET_QR_SIZE, UI_SECRET_QR_SIZE);
    lv_obj_set_pos(s_ctx.wifi_placeholder, 0, UI_SECRET_QR_Y);
    lv_obj_set_style_bg_opa(s_ctx.wifi_placeholder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_ctx.wifi_placeholder, lv_color_hex(UI_COLOR_LINE), 0);
    lv_obj_set_style_border_width(s_ctx.wifi_placeholder, 1, 0);
    lv_obj_set_style_radius(s_ctx.wifi_placeholder, 8, 0);
    lv_obj_set_scrollbar_mode(s_ctx.wifi_placeholder, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ctx.wifi_placeholder, LV_OBJ_FLAG_SCROLLABLE);

    s_ctx.wifi_qr = lv_qrcode_create(secrets);
    lv_qrcode_set_size(s_ctx.wifi_qr, UI_SECRET_QR_SIZE);
    lv_qrcode_set_dark_color(s_ctx.wifi_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(s_ctx.wifi_qr, lv_color_hex(0xffffff));
    lv_qrcode_set_quiet_zone(s_ctx.wifi_qr, true);
    lv_obj_set_pos(s_ctx.wifi_qr, 0, UI_SECRET_QR_Y);
    lv_obj_add_flag(s_ctx.wifi_qr, LV_OBJ_FLAG_HIDDEN);

    s_ctx.secret_title_label = add_label(secrets, "WiFi password", &lv_font_montserrat_16, UI_COLOR_MUTED, UI_SECRET_TEXT_W);
    s_ctx.secret_value_label = add_label(secrets, "", &lv_font_montserrat_16, UI_COLOR_TEXT, UI_SECRET_TEXT_W);

    lv_screen_load(screen);

    lv_timer_t *battery_timer = lv_timer_create(update_battery_label, BATTERY_UPDATE_PERIOD_MS, NULL);
    if (battery_timer != NULL) {
        lv_timer_ready(battery_timer);
    }
    lv_timer_t *status_timer = lv_timer_create(update_status_labels, STATUS_UPDATE_PERIOD_MS, NULL);
    if (status_timer != NULL) {
        lv_timer_ready(status_timer);
    }
}

/* ---- public API --------------------------------------------------------- */

static esp_err_t create_display(void)
{
    const board_display_info_t *info = s_ctx.display_info;
    const int32_t buf_width = info->width > info->height ? info->width : info->height;
    const size_t buf_bytes = (size_t)buf_width * LVGL_BUF_LINES * sizeof(lv_color_t);

    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t *rotate_buf = info->rotation_degrees != 0 ? heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) : NULL;
    lv_display_t *display = NULL;
    esp_err_t err = ESP_ERR_NO_MEM;

    if (buf1 == NULL || buf2 == NULL || (info->rotation_degrees != 0 && rotate_buf == NULL)) {
        ESP_LOGE(TAG, "draw buffer allocation failed");
        goto fail;
    }
    display = lv_display_create(info->width, info->height);
    if (display == NULL) {
        ESP_LOGE(TAG, "display create failed");
        goto fail;
    }
    err = board_display_create(notify_flush_ready, display, &s_ctx.panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel create failed: %s", esp_err_to_name(err));
        goto fail;
    }

    static const lv_display_rotation_t ROTATIONS[] = {
        LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270,
    };
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display, ROTATIONS[(info->rotation_degrees / 90U) % 4U]);
    lv_display_set_buffers(display, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_cb);
    if (info->even_area_alignment) {
        lv_display_add_event_cb(display, even_area_rounder_cb, LV_EVENT_INVALIDATE_AREA, display);
    }

    s_ctx.display = display;
    s_ctx.rotate_buf = rotate_buf;
    s_ctx.screen_w = lv_display_get_horizontal_resolution(display);
    s_ctx.screen_h = lv_display_get_vertical_resolution(display);
    return ESP_OK;

fail:
    if (display != NULL) {
        lv_display_delete(display);
    }
    heap_caps_free(buf1);
    heap_caps_free(buf2);
    heap_caps_free(rotate_buf);
    return err;
}

esp_err_t lvgl_ui_start(const lvgl_ui_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is required");
    if (s_running) {
        return ESP_OK;
    }
    if (!board_get_info()->has_display) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_mailbox_lock = xSemaphoreCreateMutexStatic(&s_mailbox_lock_storage);
    s_ctx.display_info = board_display_get_info();
    s_ctx.status_fn = config->status_fn;
    s_ctx.status_ctx = config->status_ctx;

    lv_init();
    ESP_RETURN_ON_ERROR(create_display(), TAG, "display init failed");

    const esp_timer_create_args_t tick_timer_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "tick timer create failed");
    if (esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000) != ESP_OK) {
        esp_timer_delete(tick_timer);
        return ESP_FAIL;
    }

    build_screen();
    ui_credentials_copy_t initial;
    copy_credentials(&initial, &config->credentials);
    s_ctx.display_on = true;
    s_ctx.last_activity_us = esp_timer_get_time();
    apply_credentials(&initial, false, s_ctx.last_activity_us);
    mbedtls_platform_zeroize(&initial, sizeof(initial));
    board_display_set_power(true);

    /* From here on only ui_task touches LVGL. */
    const BaseType_t task_ok = xTaskCreate(ui_task, "lvgl_ui", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "ui task create failed");
    s_running = true;
    return ESP_OK;
}

bool lvgl_ui_is_running(void)
{
    return s_running;
}

esp_err_t lvgl_ui_set_credentials(const lvgl_ui_credentials_t *credentials, bool reveal)
{
    ESP_RETURN_ON_FALSE(credentials != NULL, ESP_ERR_INVALID_ARG, TAG, "credentials required");
    ESP_RETURN_ON_FALSE(s_running, ESP_ERR_INVALID_STATE, TAG, "ui not running");

    xSemaphoreTake(s_mailbox_lock, portMAX_DELAY);
    copy_credentials(&s_mailbox.credentials, credentials);
    s_mailbox.credentials_pending = true;
    s_mailbox.reveal = reveal;
    xSemaphoreGive(s_mailbox_lock);
    return ESP_OK;
}

void lvgl_ui_notify_primary_press(void)
{
    if (!s_running) {
        return;
    }
    xSemaphoreTake(s_mailbox_lock, portMAX_DELAY);
    s_mailbox.presses++;
    xSemaphoreGive(s_mailbox_lock);
}

void lvgl_ui_notify_primary_hold(uint32_t held_ms, uint32_t reset_after_ms)
{
    if (!s_running) {
        return;
    }
    xSemaphoreTake(s_mailbox_lock, portMAX_DELAY);
    s_mailbox.hold_pending = true;
    s_mailbox.hold_ms = held_ms;
    s_mailbox.reset_after_ms = reset_after_ms;
    xSemaphoreGive(s_mailbox_lock);
}
