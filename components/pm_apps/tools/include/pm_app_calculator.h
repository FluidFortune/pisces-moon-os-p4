// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_calculator.h — Field calculator
//
//  Modes:
//    STD     — basic arithmetic (port of S3 calculator.cpp)
//    SUBNET  — CIDR / subnet mask helpers (new for P4 — ample
//              screen real estate makes it worth surfacing)
//
//  Core logic is platform-independent doubles; UI is LVGL.
// ============================================================

#ifndef PM_APP_CALCULATOR_H
#define PM_APP_CALCULATOR_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_calculator(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_CALCULATOR_H
