// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_wardrive_inspect.h — "Ghost Ride The Whip"
//
//  Read-only diagnostic browser for /sd/exports/wardrive_*.csv
//  and /sd/sessions/*.db files. Lets the operator verify what
//  Wardrive has actually laid down without stopping the scan.
//
//  Never modifies, never deletes. Pure rear-view.
// ============================================================

#ifndef PM_APP_WARDRIVE_INSPECT_H
#define PM_APP_WARDRIVE_INSPECT_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_wardrive_inspect(void);
#ifdef __cplusplus
}
#endif
#endif
