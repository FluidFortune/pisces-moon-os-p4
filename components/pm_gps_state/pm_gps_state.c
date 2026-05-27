// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_gps_state.c
//
//  Single shared instance, written by bridge layer, read by
//  UI apps. Writes are coarse-grained (one update per GPS
//  sentence, ~1 Hz typical) so a small mutex is fine.
// ============================================================

#include "pm_gps_state.h"
#include "pm_hal.h"
#include <string.h>

static pm_gps_t   s_gps = {0};
static pm_mutex_t s_lock = NULL;

static void _ensure_lock(void) {
    if (!s_lock) s_lock = pm_mutex_create();
}

void pm_gps_state_set(double lat, double lng, double alt_m,
                       int sats, bool valid, double speed_mps) {
    _ensure_lock();
    if (pm_mutex_take(s_lock, 50)) {
        s_gps.lat            = lat;
        s_gps.lng            = lng;
        s_gps.alt_m          = alt_m;
        s_gps.sats           = sats;
        s_gps.valid          = valid;
        s_gps.speed_mps      = speed_mps;
        s_gps.last_update_ms = pm_millis();
        s_gps.version        = s_gps.version + 1;
        pm_mutex_give(s_lock);
    }
}

void pm_gps_state_get(pm_gps_t* out) {
    if (!out) return;
    _ensure_lock();
    if (pm_mutex_take(s_lock, 50)) {
        memcpy(out, &s_gps, sizeof(*out));
        pm_mutex_give(s_lock);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

bool pm_gps_state_fresh(uint32_t max_age_ms) {
    pm_gps_t snap;
    pm_gps_state_get(&snap);
    if (snap.last_update_ms == 0) return false;
    return (pm_millis() - snap.last_update_ms) <= max_age_ms;
}
