# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_app_snake.c"
        "pm_app_pacman.c"
        "pm_app_galaga.c"
        "pm_app_chess.c"
        "pm_app_doom.c"
        "pm_app_simcity.c"
        "pm_app_retro_elf.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_nosql
)
