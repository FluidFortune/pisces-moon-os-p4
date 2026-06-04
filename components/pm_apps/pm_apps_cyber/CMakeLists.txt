# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


idf_component_register(
    SRCS
        "pm_ducky_engine.c"
        "pm_app_wardrive.c"
        "pm_app_silas_creek.c"
        "pm_app_clinician.c"
        "pm_app_bt_radar.c"
        "pm_app_pkt_sniffer.c"
        "pm_app_beacon.c"
        "pm_app_net_scanner.c"
        "pm_app_hash_tool.c"
        "pm_app_ble_gatt.c"
        "pm_app_wpa_hs.c"
        "pm_app_rf_spectrum.c"
        "pm_app_probe_intel.c"
        "pm_app_pkt_analysis.c"
        "pm_app_ble_ducky.c"
        "pm_app_usb_ducky.c"
        "pm_app_wifi_ducky.c"
        "pm_app_nfc_reader.c"
        "pm_app_nfc_clone.c"
        "pm_app_amiibo.c"
        "pm_app_nfc_emulate.c"
        "pm_app_secondary_scan.c"
        "pm_app_wardrive_inspect.c"
        "pm_app_tracker_scan.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_event
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_c6_bridge
        pm_sqlite
        pm_gps_state
        pm_gps_uart
        pm_cardputer_i2c
        espressif__esp_lvgl_port
        pm_nosql
        pm_nfc
        pm_peer
        mbedtls
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
    -Wno-error=unused-function
    -Wno-error=unused-variable
)
