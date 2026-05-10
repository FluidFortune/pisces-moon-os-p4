# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "main.c"
        "pm_app.c"
        "pm_launcher.c"
        "pm_apps_register.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_system
        freertos
        pm_hal
        pm_bsp
        pm_ui
        pm_c6_bridge
        pm_app_iface
        pm_nosql
        pm_audio
        pm_gps_state
        pm_gps_uart
        pm_radio
        pm_lora
        pm_sqlite
        pm_apps_system
        pm_apps_tools
        pm_apps_intel
        pm_apps_games
        pm_apps_media
        pm_apps_comms
        pm_apps_cyber
        lvgl__lvgl
)
