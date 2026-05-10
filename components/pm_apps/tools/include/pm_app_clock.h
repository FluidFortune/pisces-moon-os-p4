// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_clock.h — Big digital clock + stopwatch
//
//  Time source priority:
//    1. GPS time mirrored from C6 (NMEA RMC sentence — most
//       accurate, works without internet).
//    2. NTP via WiFi (fallback when GPS lock is unavailable).
//    3. Uptime fallback when neither is available.
//
//  P4 layout uses the full 1024×600 — clock face is huge.
// ============================================================

#ifndef PM_APP_CLOCK_H
#define PM_APP_CLOCK_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_clock(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_CLOCK_H
