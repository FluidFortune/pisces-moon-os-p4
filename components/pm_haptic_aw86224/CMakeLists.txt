# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later

idf_component_register(
    SRCS "pm_haptic_aw86224.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        log
        pm_bsp
        pm_board
)
