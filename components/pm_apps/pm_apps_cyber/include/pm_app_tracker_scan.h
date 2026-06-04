// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_tracker_scan.h — AirTag / Tile / SmartTag detector
//
//  "Am I being followed?" passive tracker scanner. Filters
//  BLE advertisements by manufacturer pattern and surfaces
//  devices that match known tracker classes.
// ============================================================

#ifndef PM_APP_TRACKER_SCAN_H
#define PM_APP_TRACKER_SCAN_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_tracker_scan(void);
#ifdef __cplusplus
}
#endif
#endif
