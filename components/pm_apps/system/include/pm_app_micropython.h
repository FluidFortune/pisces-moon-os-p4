// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_micropython.h — Embedded MicroPython REPL
//
//  Phase 2 stub. MicroPython port to ESP32-P4 + ESP-IDF 5.4.x
//  is not packaged as a stock component; integrating it is a
//  separate effort. This app reserves the slot, provides a
//  scrollback / input UI in the launcher, and reports status.
//
//  Future: link micropython as a static lib, expose a Python
//  module 'pm' that wraps pm_hal calls (sd, gps, c6) so user
//  scripts can drive the device from the REPL.
// ============================================================

#ifndef PM_APP_MICROPYTHON_H
#define PM_APP_MICROPYTHON_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_micropython(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_MICROPYTHON_H
