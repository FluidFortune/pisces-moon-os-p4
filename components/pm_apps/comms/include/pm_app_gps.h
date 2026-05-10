// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gps.h — Live GPS readout
//
//  P4 architecture: GPS is parsed by the C6 from the Crowtail
//  UART. The bridge layer pushes "gps" events into pm_gps_state.
//  This app just reads from the shared state and renders.
// ============================================================

#ifndef PM_APP_GPS_H
#define PM_APP_GPS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_gps(void);
#ifdef __cplusplus
}
#endif
#endif
