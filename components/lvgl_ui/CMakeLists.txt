set(srcs "src/ui_status_format.c")
if(CONFIG_SERIALITE_BOARD_HAS_DISPLAY)
    list(APPEND srcs "src/lvgl_ui.c")
else()
    list(APPEND srcs "src/lvgl_ui_headless.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        board
        credentials
        esp_lcd
        esp_timer
        lvgl
        secret_display
)
