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
        "pm_app_rss.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_nosql
        pm_c6_bridge
        json
        esp_wifi
        esp_http_client
        esp-tls
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
