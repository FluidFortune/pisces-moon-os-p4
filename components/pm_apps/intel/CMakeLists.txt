# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com

idf_component_register(
    SRCS
        "pm_ref_browser.c"
        "pm_app_terminal.c"
        "pm_app_gemini_log.c"
        "pm_app_ref_med.c"
        "pm_app_ref_surv.c"
        "pm_app_baseball.c"
        "pm_app_trails.c"
        "pm_app_ssh.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_nosql
        pm_c6_bridge
        json
)
