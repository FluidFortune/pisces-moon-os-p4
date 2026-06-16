# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "pm_c5_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_common
        freertos
        log
        pm_hal
        pm_cardputer_i2c   # PMU1 data structs reused as-is
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format=
    -Wno-error=format-truncation=
    -Wno-error=misleading-indentation
    -Wno-error=maybe-uninitialized
    -Wno-error=incompatible-pointer-types
)
