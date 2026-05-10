# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_app_gps.c"
        "pm_app_wifi.c"
        "pm_app_bluetooth.c"
        "pm_app_lora_voice.c"
        "pm_app_mesh_messenger.c"
        "pm_app_voice_terminal.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_radio
        pm_lora
        pm_hal
        pm_app_iface
        pm_c6_bridge
        pm_nosql
        pm_audio
        pm_gps_state
        json
)
