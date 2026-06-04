idf_component_register(
    SRCS "pm_boot.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lvgl_port lvgl freertos esp_common log pm_board pm_ui
)
