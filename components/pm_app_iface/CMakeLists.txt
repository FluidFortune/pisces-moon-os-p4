# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


# pm_app_iface — interface-only component.
# Provides pm_app.h to apps and the launcher without forcing
# a back-reference into main/.
idf_component_register(
    INCLUDE_DIRS "include"
)
