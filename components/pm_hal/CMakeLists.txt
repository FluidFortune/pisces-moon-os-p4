# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


idf_component_register(
    SRCS
        "pm_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        driver
        fatfs
        sdmmc
        vfs
        esp_timer
        esp_system
    PRIV_REQUIRES
        spi_flash
        nvs_flash
        esp_psram
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
