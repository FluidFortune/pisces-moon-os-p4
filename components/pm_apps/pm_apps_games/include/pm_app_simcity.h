// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_simcity.h — City builder (Pisces Moon original)
//
//  Custom city-building sim. Tile grid, zones (residential /
//  commercial / industrial / road / park), simple population
//  + cash economy, save/load to SD.
//
//  This is the Pisces Moon implementation, not Maxis SimCity.
// ============================================================

#ifndef PM_APP_SIMCITY_H
#define PM_APP_SIMCITY_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_simcity(void);
void pm_app_simcity_save(void);
void pm_app_simcity_load(void);
void pm_app_simcity_tile_tap(int gx, int gy);
void pm_app_simcity_set_tool(int tool);
#ifdef __cplusplus
}
#endif
#endif
