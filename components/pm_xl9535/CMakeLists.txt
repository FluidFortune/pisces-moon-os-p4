# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "pm_xl9535.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        freertos
        log
        pm_board
        pm_bsp
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format=
    -Wno-error=format-truncation=
    -Wno-error=misleading-indentation
    -Wno-error=maybe-uninitialized
    -Wno-error=incompatible-pointer-types
)
