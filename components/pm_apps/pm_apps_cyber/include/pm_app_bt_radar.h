// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_bt_radar.h — CYBER's session-aware BT radar
//
//  Differs from COMMS/bluetooth (live casual view): this is
//  a session-aware, SQLite-backed radar with start/stop,
//  RSSI heatmap rolling history, and CSV export.
//
//  Reads the same C6 ble_seen events but logs each to the
//  current wardrive session DB plus its own ring of recent
//  observations for the heatmap UI.
// ============================================================

#ifndef PM_APP_BT_RADAR_H
#define PM_APP_BT_RADAR_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_bt_radar(void);
void pm_app_bt_radar_on_seen(const char* mac, const char* name,
                              int rssi, const char* addr_type,
                              const char* mfg);
#ifdef __cplusplus
}
#endif
#endif
