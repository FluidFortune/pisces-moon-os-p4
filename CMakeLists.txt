# Pisces Moon OS
# Copyright (C) 2026 Eric Becker / Fluid Fortune
# SPDX-License-Identifier: AGPL-3.0-or-later
# Contributions: see CLA.md
# fluidfortune.com


cmake_minimum_required(VERSION 3.16)

set(PM_P4_BOARD "$ENV{PM_P4_BOARD}" CACHE STRING
    "Pisces Moon P4 board profile: elecrow_p4_7 or elecrow_p4_5")
if(PM_P4_BOARD STREQUAL "")
    set(PM_P4_BOARD "elecrow_p4_7")
endif()

if(PM_P4_BOARD MATCHES "^(5|p4_5|elecrow_p4_5|5inch)$")
    add_compile_definitions(PM_BOARD_PROFILE_ELECROW_P4_5=1)
    message(STATUS "Pisces Moon board profile: elecrow_p4_5")
else()
    add_compile_definitions(PM_BOARD_PROFILE_ELECROW_P4_7=1)
    message(STATUS "Pisces Moon board profile: elecrow_p4_7")
endif()

# pm_apps_* components live one level deeper than ESP-IDF auto-discovers.
# Point EXTRA_COMPONENT_DIRS at each category folder explicitly so ESP-IDF
# registers them as pm_apps_system, pm_apps_tools, etc. (matching main's
# REQUIRES list).
set(EXTRA_COMPONENT_DIRS
    "components/pm_apps/pm_apps_system"
    "components/pm_apps/pm_apps_tools"
    "components/pm_apps/pm_apps_intel"
    "components/pm_apps/pm_apps_games"
    "components/pm_apps/pm_apps_media"
    "components/pm_apps/pm_apps_comms"
    "components/pm_apps/pm_apps_cyber"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pisces_moon_p4)
