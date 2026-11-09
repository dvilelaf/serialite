#include "buttons.h"

#include "app_watchdog.h"
#include "board.h"
#include "button_gesture.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "buttons";

#define BUTTONS_TASK_STACK 4096
#define BUTTONS_TASK_PRIORITY 3
#define BUTTONS_POLL_MS 50

static buttons_config_t s_config;
static bool s_started;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

#define CALL(cb, ...) do { if (s_config.cb != NULL) { s_config.cb(__VA_ARGS__); } } while (0)

static void poll_primary(button_gesture_t *gesture, bool *was_held, uint64_t now)
{
    const button_gesture_event_t event = button_gesture_update(gesture, board_button_pressed(BOARD_BUTTON_PRIMARY), now);
    if (event == BUTTON_GESTURE_PRESSED) {
        CALL(on_primary_press, s_config.ctx);
    } else if (event == BUTTON_GESTURE_LONG_HOLD) {
        CALL(on_factory_reset, s_config.ctx);
    }

    const uint32_t held_ms = button_gesture_held_ms(gesture, now);
    if (held_ms > 0 || *was_held) {
        CALL(on_primary_hold, held_ms, s_config.ctx);
    }
    *was_held = held_ms > 0;
}

static void poll_secondary(button_gesture_t *gesture, uint64_t now)
{
    const button_gesture_event_t event = button_gesture_update(gesture, board_button_pressed(BOARD_BUTTON_SECONDARY), now);
    if (event == BUTTON_GESTURE_SHORT_RELEASE) {
        CALL(on_secondary_press, s_config.ctx);
    } else if (event == BUTTON_GESTURE_LONG_HOLD) {
        CALL(on_secondary_hold, s_config.ctx);
    }
}

static void buttons_task(void *arg)
{
    (void)arg;
    const bool watchdog = app_watchdog_register_current_task("buttons") == ESP_OK;
    const board_info_t *info = board_get_info();
    button_gesture_t primary;
    button_gesture_t secondary;
    button_gesture_init(&primary, BUTTONS_FACTORY_RESET_HOLD_MS);
    button_gesture_init(&secondary, BUTTONS_POWER_OFF_HOLD_MS);
    bool primary_was_held = false;

    while (true) {
        if (watchdog) {
            app_watchdog_reset_current_task();
        }
        const uint64_t now = now_ms();
        if (info->buttons[BOARD_BUTTON_PRIMARY]) {
            poll_primary(&primary, &primary_was_held, now);
        }
        if (info->buttons[BOARD_BUTTON_SECONDARY]) {
            poll_secondary(&secondary, now);
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTONS_POLL_MS));
    }
}

esp_err_t buttons_start(const buttons_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "missing config");
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = *config;
    const BaseType_t ok = xTaskCreate(buttons_task, "buttons", BUTTONS_TASK_STACK, NULL, BUTTONS_TASK_PRIORITY, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    s_started = true;
    return ESP_OK;
}
