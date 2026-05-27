# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS "pm_cardputer_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        driver
        esp_driver_uart
        pm_bsp
        pm_gps_state
        pm_hal
        pm_input
)

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
