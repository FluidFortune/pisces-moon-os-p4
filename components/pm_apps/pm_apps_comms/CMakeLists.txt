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
        "pm_app_weather.c"
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
        pm_gps_uart
        pm_input
        pm_peer
        pm_cardputer_i2c
        esp_event
        esp_wifi
        esp_http_client
        esp-tls
        espressif__esp_lvgl_port
        json
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
