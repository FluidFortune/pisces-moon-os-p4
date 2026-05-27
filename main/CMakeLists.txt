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
        esp_event
        esp_netif
        esp_wifi
        bt
        espressif__esp_hosted
        espressif__esp_lvgl_port
        freertos
        pm_board
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
        pm_peer
        pm_tbeam
        pm_nfc
        pm_camera
        pm_input
        pm_lora
        pm_sqlite
        pm_boot
        pm_apps_system
        pm_apps_tools
        pm_apps_intel
        pm_apps_games
        pm_apps_media
        pm_apps_comms
        pm_apps_cyber
        lvgl__lvgl
)
