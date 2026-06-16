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
        # "pm_app_c6_flasher.c"  # Unlinked Phase 17: deprecated C6 firmware OTA. Preserved on disk.
    INCLUDE_DIRS "include"
    REQUIRES
        pm_ui
        lvgl__lvgl
        pm_hal
        pm_c6_bridge
        # pm_c6_programmer  # Unlinked Phase 17: deprecated C6 firmware OTA driver.
        pm_app_iface
        pm_input
        nvs_flash
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
