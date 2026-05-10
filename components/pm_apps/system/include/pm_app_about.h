// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_about.h — System / About panel
//
//  Static info screen: version, license, hardware, credits,
//  free heap / PSRAM stats. Read-only.
// ============================================================

#ifndef PM_APP_ABOUT_H
#define PM_APP_ABOUT_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_about(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_ABOUT_H
