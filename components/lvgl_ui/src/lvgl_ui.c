#include "lvgl_ui.h"

#include "board_waveshare_amoled.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "terminal_bridge.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lvgl_ui";

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUF_LINES 20
#define LVGL_TASK_MIN_DELAY_MS 2
#define LVGL_TASK_MAX_DELAY_MS 50
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2
#define TOUCH_ENABLE_POLL_FALLBACK 0
#define TOUCH_FALLBACK_POLL_TICKS 100
#define UI_RX_QUEUE_DEPTH 12
#define UI_RX_CHUNK_MAX 128
#define UI_TERMINAL_TEXT_MAX 4096

typedef struct {
    size_t len;
    char data[UI_RX_CHUNK_MAX + 1];
} ui_rx_chunk_t;

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    SemaphoreHandle_t mutex;
    QueueHandle_t rx_queue;
    lv_obj_t *terminal_label;
    lv_obj_t *input_area;
    bool running;
} lvgl_ui_context_t;

static lvgl_ui_context_t s_ctx;
static char s_terminal_text[UI_TERMINAL_TEXT_MAX];
static size_t s_terminal_text_len;

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
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel draw failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_point_data_t point[1] = {0};
    uint8_t count = 0;
#if TOUCH_ENABLE_POLL_FALLBACK
    static uint32_t fallback_counter;
#endif

#if TOUCH_ENABLE_POLL_FALLBACK
    if (!board_waveshare_amoled_touch_signal_active() && ++fallback_counter < TOUCH_FALLBACK_POLL_TICKS) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    fallback_counter = 0;
#else
    if (!board_waveshare_amoled_touch_signal_active()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
#endif

    esp_lcd_touch_read_data(touch);
    if (esp_lcd_touch_get_data(touch, point, &count, 1) == ESP_OK && count > 0) {
        data->point.x = point[0].x;
        data->point.y = point[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
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
            ui_rx_chunk_t chunk;
            while (s_ctx.rx_queue != NULL && xQueueReceive(s_ctx.rx_queue, &chunk, 0) == pdTRUE) {
                if (s_ctx.terminal_label != NULL) {
                    if (chunk.len >= UI_TERMINAL_TEXT_MAX) {
                        chunk.len = UI_TERMINAL_TEXT_MAX - 1;
                    }
                    if (s_terminal_text_len + chunk.len >= UI_TERMINAL_TEXT_MAX) {
                        const size_t keep = UI_TERMINAL_TEXT_MAX / 2;
                        memmove(s_terminal_text, s_terminal_text + s_terminal_text_len - keep, keep);
                        s_terminal_text_len = keep;
                    }
                    memcpy(s_terminal_text + s_terminal_text_len, chunk.data, chunk.len);
                    s_terminal_text_len += chunk.len;
                    s_terminal_text[s_terminal_text_len] = '\0';
                    lv_label_set_text(s_ctx.terminal_label, s_terminal_text);
                    lv_obj_scroll_to_y(lv_obj_get_parent(s_ctx.terminal_label), LV_COORD_MAX, LV_ANIM_OFF);
                }
            }
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

static void add_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_width(label, BOARD_LCD_H_RES - 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

static void submit_bytes(const char *data, size_t len)
{
    if (data != NULL && len > 0) {
        terminal_bridge_submit_input(TERMINAL_BRIDGE_SOURCE_LVGL, (const uint8_t *)data, len);
    }
}

static void quick_button_cb(lv_event_t *event)
{
    const char *payload = (const char *)lv_event_get_user_data(event);
    submit_bytes(payload, strlen(payload));
}

static void send_input_cb(lv_event_t *event)
{
    (void)event;
    if (s_ctx.input_area == NULL) {
        return;
    }

    const char *text = lv_textarea_get_text(s_ctx.input_area);
    submit_bytes(text, strlen(text));
    submit_bytes("\r", 1);
    lv_textarea_set_text(s_ctx.input_area, "");
}

static void keyboard_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        send_input_cb(event);
    }
}

static void add_quick_button(lv_obj_t *parent, const char *label, const char *payload)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 34);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x14392d), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2ee6b8), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_add_event_cb(button, quick_button_cb, LV_EVENT_CLICKED, (void *)payload);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, label);
    lv_obj_center(button_label);
}

static void bridge_output_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (s_ctx.rx_queue == NULL || data == NULL || len == 0) {
        return;
    }

    size_t offset = 0;
    while (offset < len) {
        ui_rx_chunk_t chunk = {0};
        chunk.len = len - offset;
        if (chunk.len > UI_RX_CHUNK_MAX) {
            chunk.len = UI_RX_CHUNK_MAX;
        }
        memcpy(chunk.data, data + offset, chunk.len);
        chunk.data[chunk.len] = '\0';
        if (xQueueSend(s_ctx.rx_queue, &chunk, 0) != pdTRUE) {
            return;
        }
        offset += chunk.len;
    }
}

static void build_boot_screen(const lvgl_ui_boot_status_t *status)
{
    const char *ssid = status->ssid != NULL ? status->ssid : "(sin ssid)";
    const char *password = status->password != NULL ? status->password : "(sin password)";
    const char *ip_addr = status->ip_addr != NULL ? status->ip_addr : "192.168.4.1";

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07110e), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x14392d), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(screen, 14, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    add_label(screen, "ESP32-KVM", &lv_font_montserrat_16, lv_color_hex(0x79ffd8));
    add_label(screen, "Consola de rescate local", &lv_font_montserrat_16, lv_color_hex(0xd8fff4));

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_width(card, BOARD_LCD_H_RES - 28);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0d211b), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2ee6b8), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_margin_top(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    char line[128];
    snprintf(line, sizeof(line), "WiFi AP: %s", ssid);
    add_label(card, line, &lv_font_montserrat_16, lv_color_hex(0xffffff));
    snprintf(line, sizeof(line), "Password: %s", password);
    add_label(card, line, &lv_font_montserrat_16, lv_color_hex(0xffffff));
    snprintf(line, sizeof(line), "Web: http://%s", ip_addr);
    add_label(card, line, &lv_font_montserrat_16, lv_color_hex(0xb9ffe9));
    snprintf(line, sizeof(line), "USB: %s", status->usb_connected ? "conectado" : "desconectado");
    add_label(card, line, &lv_font_montserrat_16, lv_color_hex(0xffd37a));

    add_label(screen,
              "La password solo se muestra localmente. No se escribe en logs ni en NVS sin cifrado.",
              &lv_font_montserrat_16,
              lv_color_hex(0x9fb8b0));

    lv_obj_t *terminal_box = lv_obj_create(screen);
    lv_obj_set_width(terminal_box, BOARD_LCD_H_RES - 28);
    lv_obj_set_height(terminal_box, 120);
    lv_obj_set_style_bg_color(terminal_box, lv_color_hex(0x020604), 0);
    lv_obj_set_style_border_color(terminal_box, lv_color_hex(0x1f8f75), 0);
    lv_obj_set_style_border_width(terminal_box, 1, 0);
    lv_obj_set_style_pad_all(terminal_box, 8, 0);
    lv_obj_set_scroll_dir(terminal_box, LV_DIR_VER);

    s_ctx.terminal_label = lv_label_create(terminal_box);
    lv_obj_set_width(s_ctx.terminal_label, BOARD_LCD_H_RES - 54);
    lv_obj_set_style_text_font(s_ctx.terminal_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ctx.terminal_label, lv_color_hex(0xd8fff4), 0);
    lv_label_set_long_mode(s_ctx.terminal_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ctx.terminal_label, "[terminal local listo]\n");

    lv_obj_t *quick_row = lv_obj_create(screen);
    lv_obj_set_width(quick_row, BOARD_LCD_H_RES - 28);
    lv_obj_set_height(quick_row, 46);
    lv_obj_set_style_bg_opa(quick_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(quick_row, 0, 0);
    lv_obj_set_style_pad_all(quick_row, 0, 0);
    lv_obj_set_flex_flow(quick_row, LV_FLEX_FLOW_ROW_WRAP);
    add_quick_button(quick_row, "Ctrl+C", "\x03");
    add_quick_button(quick_row, "Ctrl+D", "\x04");
    add_quick_button(quick_row, "Enter", "\r");
    add_quick_button(quick_row, "Esc", "\x1b");
    add_quick_button(quick_row, "Tab", "\t");

    lv_obj_t *input_row = lv_obj_create(screen);
    lv_obj_set_width(input_row, BOARD_LCD_H_RES - 28);
    lv_obj_set_height(input_row, 46);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);

    s_ctx.input_area = lv_textarea_create(input_row);
    lv_obj_set_width(s_ctx.input_area, BOARD_LCD_H_RES - 104);
    lv_obj_set_height(s_ctx.input_area, 42);
    lv_textarea_set_one_line(s_ctx.input_area, true);
    lv_textarea_set_placeholder_text(s_ctx.input_area, "comando");

    lv_obj_t *send_button = lv_button_create(input_row);
    lv_obj_set_width(send_button, 66);
    lv_obj_set_height(send_button, 42);
    lv_obj_add_event_cb(send_button, send_input_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send_label = lv_label_create(send_button);
    lv_label_set_text(send_label, "Send");
    lv_obj_center(send_label);

    lv_obj_t *keyboard = lv_keyboard_create(screen);
    lv_obj_set_width(keyboard, BOARD_LCD_H_RES - 28);
    lv_obj_set_height(keyboard, 132);
    lv_keyboard_set_textarea(keyboard, s_ctx.input_area);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);

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
        BOARD_LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(
        BOARD_LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t),
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
    lv_display_set_buffers(
        display,
        buf1,
        buf2,
        BOARD_LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_cb);

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_err = board_waveshare_amoled_new_touch(&touch);
    if (touch_err == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        ESP_RETURN_ON_FALSE(indev != NULL, ESP_ERR_NO_MEM, TAG, "lvgl indev create failed");
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, touch);
        lv_indev_set_read_cb(indev, touch_cb);
    } else {
        ESP_LOGW(TAG, "touch init failed: %s", esp_err_to_name(touch_err));
    }

    const esp_timer_create_args_t tick_timer_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "tick timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), TAG, "tick timer start failed");

    s_ctx.mutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_ctx.mutex != NULL, ESP_ERR_NO_MEM, TAG, "lvgl mutex create failed");
    s_ctx.rx_queue = xQueueCreate(UI_RX_QUEUE_DEPTH, sizeof(ui_rx_chunk_t));
    ESP_RETURN_ON_FALSE(s_ctx.rx_queue != NULL, ESP_ERR_NO_MEM, TAG, "ui rx queue create failed");
    ESP_RETURN_ON_ERROR(terminal_bridge_register_output_callback(bridge_output_cb, NULL), TAG, "bridge callback failed");
    s_ctx.display = display;
    s_ctx.panel = panel;
    s_ctx.touch = touch;

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
