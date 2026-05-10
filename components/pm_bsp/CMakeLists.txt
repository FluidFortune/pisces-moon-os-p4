# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS "pm_bsp.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_hal
        driver
        esp_lcd
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_i2c
    PRIV_REQUIRES
        espressif__esp_lcd_ili9881c
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_gt911
        espressif__esp_lvgl_port
        lvgl__lvgl
)
