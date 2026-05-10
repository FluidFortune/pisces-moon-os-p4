# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_c6_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        driver
        json
        pm_hal
)
