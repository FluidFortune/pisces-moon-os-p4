# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_app_about.c"
        "pm_app_system.c"
        "pm_app_files.c"
        "pm_app_filemgr.c"
        "pm_app_elf_browser.c"
        "pm_app_gamepad.c"
        "pm_app_bridge.c"
        "pm_app_micropython.c"
        "pm_app_c6_flasher.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_c6_bridge
        pm_c6_programmer
        pm_app_iface
        nvs_flash
)
