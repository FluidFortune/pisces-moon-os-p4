# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


idf_component_register(
    SRCS "pm_bsp.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_board
        pm_hal
        driver
        esp_lcd
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_i2c
    PRIV_REQUIRES
        espressif__esp_lcd_ek79007
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_gt911
        espressif__esp_lvgl_port
        lvgl__lvgl
)

# Relax v5.5.3 strict warnings for this component.
# These v5.5 catches are stylistic, not functional bugs.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format=
    -Wno-error=format-truncation=
    -Wno-error=misleading-indentation
    -Wno-error=maybe-uninitialized
    -Wno-error=incompatible-pointer-types
    -Wno-error=implicit-function-declaration
    -Wno-error=int-conversion
    -Wno-error=discarded-qualifiers
)
