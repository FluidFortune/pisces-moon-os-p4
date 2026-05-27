// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_gps_state.h — Shared GPS state cache
//
//  GPS writers push fixes into this cache. The default P4 path
//  is the Cardputer ADV UART1 header bridge; the older P4-direct
//  BN-180 UART reader remains available for bench experiments.
//  Any app that needs GPS reads from here — wardrive, gps_app,
//  trails (for nearby search), bridge_app status, etc.
//
//  Last-update timestamp lets apps detect stale data.
//
//  Thread-safety: writes happen on the GPS task; reads on
//  the UI loop. We use a simple "version" counter that the
//  reader can poll (atomic), avoiding a mutex on the read path.
//  Acceptable because all fields are independently readable —
//  if one is stale by 100ms it doesn't break anything.
// ============================================================

#ifndef PM_GPS_STATE_H
#define PM_GPS_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double  lat;
    double  lng;
    double  alt_m;
    int     sats;
    bool    valid;
    double  speed_mps;
    uint32_t last_update_ms;     // pm_millis() at last update; 0 = never
    uint32_t version;            // increments every update
} pm_gps_t;

// Push from bridge layer.
void pm_gps_state_set(double lat, double lng, double alt_m,
                       int sats, bool valid, double speed_mps);

// Snapshot for readers.
void pm_gps_state_get(pm_gps_t* out);

// Convenience: true if last update was within `max_age_ms`.
bool pm_gps_state_fresh(uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif

#endif  // PM_GPS_STATE_H
