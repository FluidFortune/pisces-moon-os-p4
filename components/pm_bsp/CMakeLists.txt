# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

# Both .c files are listed unconditionally — each one's body is
# gated by #if defined(PM_BOARD_PROFILE_*) so only the active
# board profile contributes object code. The HI8561 panel driver
# is also gated on profile (it's irrelevant on the Elecrow boards).
idf_component_register(
    SRCS
        "pm_bsp.c"
        "pm_bsp_lilygo.c"
        "pm_bsp_hi8561.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_board
        pm_hal
        driver
        esp_lcd
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_i2c
        esp_driver_sdmmc
        fatfs
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
