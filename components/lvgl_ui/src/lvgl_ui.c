#include "lvgl_ui.h"

#include "board_waveshare_amoled.h"
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

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t mutex;
    bool running;
} lvgl_ui_context_t;

static lvgl_ui_context_t s_ctx;

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

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(display);
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
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

    area->x1 = (area->x1 < 0) ? 0 : ((area->x1 >> 1) << 1);
    area->y1 = (area->y1 < 0) ? 0 : ((area->y1 >> 1) << 1);
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
    if (area->x2 >= BOARD_LCD_H_RES) {
        area->x2 = BOARD_LCD_H_RES - 1;
    }
    if (area->y2 >= BOARD_LCD_V_RES) {
        area->y2 = BOARD_LCD_V_RES - 1;
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

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (lock_lvgl(-1)) {
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
    const char *ip_addr = status->ip_addr != NULL ? status->ip_addr : "192.168.4.1";

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x06130f), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *left = lv_obj_create(screen);
    lv_obj_set_size(left, 150, UI_LANDSCAPE_H - 32);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    add_label(left, "KVM", &lv_font_montserrat_28, lv_color_hex(0x7dffe1), 150);
    add_label(left, "rescue", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), 150);

    lv_obj_t *usb_pill = lv_obj_create(left);
    lv_obj_set_width(usb_pill, 132);
    lv_obj_set_height(usb_pill, 40);
    lv_obj_set_style_radius(usb_pill, 20, 0);
    lv_obj_set_style_border_width(usb_pill, 1, 0);
    lv_obj_set_style_pad_all(usb_pill, 0, 0);
    if (status->usb_connected) {
        lv_obj_set_style_bg_color(usb_pill, lv_color_hex(0x08291f), 0);
        lv_obj_set_style_border_color(usb_pill, lv_color_hex(0x2ee6b8), 0);
    } else {
        lv_obj_set_style_bg_color(usb_pill, lv_color_hex(0x2a1f08), 0);
        lv_obj_set_style_border_color(usb_pill, lv_color_hex(0xffc65c), 0);
    }

    lv_obj_t *usb_label = lv_label_create(usb_pill);
    lv_label_set_text(usb_label, status->usb_connected ? "USB OK" : "USB WAIT");
    lv_obj_set_style_text_font(usb_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(usb_label, status->usb_connected ? lv_color_hex(0x7dffe1) : lv_color_hex(0xffd37a), 0);
    lv_obj_center(usb_label);

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, UI_LANDSCAPE_W - 186, UI_LANDSCAPE_H - 32);
    lv_obj_align(card, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x030807), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x12362d), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    char line[128];
    add_label(card, "WiFi", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_LANDSCAPE_W - 224);
    snprintf(line, sizeof(line), "%s", ssid);
    add_label(card, line, &lv_font_montserrat_28, lv_color_hex(0xffffff), UI_LANDSCAPE_W - 224);

    add_label(card, "Password", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_LANDSCAPE_W - 224);
    add_label(card, password, &lv_font_montserrat_20, lv_color_hex(0xffffff), UI_LANDSCAPE_W - 224);

    add_label(card, "Web terminal", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_LANDSCAPE_W - 224);
    snprintf(line, sizeof(line), "http://%s", ip_addr);
    add_label(card, line, &lv_font_montserrat_20, lv_color_hex(0x7dffe1), UI_LANDSCAPE_W - 224);

    add_label(card, "Web only. Use phone/laptop.", &lv_font_montserrat_16, lv_color_hex(0x6b8f85), UI_LANDSCAPE_W - 224);

    lv_screen_load(screen);
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
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    lv_display_set_buffers(
        display,
        buf1,
        buf2,
        LVGL_DRAW_BUF_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_add_event_cb(display, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

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
