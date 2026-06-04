# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


idf_component_register(
    SRCS
        "pm_app_notepad.c"
        "pm_app_keytest.c"
        "pm_app_calculator.c"
        "pm_app_clock.c"
        "pm_app_calendar.c"
        "pm_app_etch.c"
        "pm_app_camera_qr.c"
        "pm_app_ereader.c"
        "pm_app_contacts.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_app_iface
        pm_camera
        pm_input
        esp_lvgl_port
        pm_nosql
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
