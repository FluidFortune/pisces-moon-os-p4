# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_ducky_engine.c"
        "pm_app_wardrive.c"
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
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_c6_bridge
        pm_sqlite
        pm_gps_state
        pm_nosql
        mbedtls
)
