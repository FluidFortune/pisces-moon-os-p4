// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_bluetooth.h — BLE scanner / device list
//
//  Live list of BLE devices seen by the C6 Ghost Engine.
//  This is *consumer-side only* — actual BLE scanning is
//  always running on the C6, and ble_seen events flow
//  through pm_c6_bridge.
//
//  This app:
//    - subscribes its callback to incoming ble_seen events
//    - keeps a rolling deduped table (up to 128 devices)
//    - shows RSSI, name, MAC, addr_type, mfg_data preview
//    - sortable: by name / by RSSI / by last-seen
// ============================================================

#ifndef PM_APP_BLUETOOTH_H
#define PM_APP_BLUETOOTH_H
#include "pm_app.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_bluetooth(void);
void pm_app_bluetooth_on_seen(const char* mac, const char* name,
                                int rssi, const char* addr_type,
                                const char* mfg);
#ifdef __cplusplus
}
#endif
#endif
