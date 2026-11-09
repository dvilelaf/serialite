set(srcs)
if(CONFIG_SERIALITE_BOARD_WAVESHARE_AMOLED_1_8)
    list(APPEND srcs "src/board_waveshare_amoled_1_8.c")
elseif(CONFIG_SERIALITE_BOARD_GENERIC_HEADLESS)
    list(APPEND srcs "src/board_generic_headless.c")
endif()

# Component requirements cannot depend on Kconfig, so every board's drivers
# are listed; only the selected board's source is compiled.
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_lcd
    PRIV_REQUIRES
        esp_lcd_sh8601
        esp_io_expander_tca9554
        log
)
