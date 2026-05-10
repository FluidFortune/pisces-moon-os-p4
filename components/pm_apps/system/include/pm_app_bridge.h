// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_bridge.h — Upstream Bridge control panel
//
//  Two distinct "bridges" exist in Pisces Moon P4:
//
//    1. C6 ↔ P4 Bridge (UART2, internal)
//       Always on. Owned by pm_c6_bridge.
//
//    2. P4 ↔ Jennifer Bridge (USB-C, external)
//       Toggle on/off from this app. When ON, Pisces Moon P4
//       forwards C6 events upstream and answers commands
//       from pm_bridge.py (status, wardrive_start, etc.).
//
//  This app's job is to:
//    - Show connected/disconnected to the upstream host
//    - Show recent commands received and events emitted
//    - Toggle four streams independently:
//        WIFI / BLE / PROBE / PKT
//      so the host can subscribe to a subset.
//    - Show GPS state (mirrored from C6)
//
//  Mirrors the S3 bridge_app.cpp toggle panel.
// ============================================================

#ifndef PM_APP_BRIDGE_H
#define PM_APP_BRIDGE_H

#include "pm_app.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi;
    bool ble;
    bool probe;
    bool pkt;
} pm_bridge_streams_t;

const pm_bridge_streams_t* pm_bridge_streams(void);
void pm_bridge_set_streams(const pm_bridge_streams_t* s);

bool pm_bridge_is_connected(void);
bool pm_bridge_is_streaming(void);

const pm_app_t* pm_app_bridge(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_BRIDGE_H
