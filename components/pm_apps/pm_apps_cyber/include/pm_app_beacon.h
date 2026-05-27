// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_beacon.h — Beacon spotter
// Filtered wifi_seen viewer focused on beacon detail (vendor IEs,
// CIPHER suites, 802.11r/k/v capability bits when C6 includes them).
#ifndef PM_APP_BEACON_H
#define PM_APP_BEACON_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_beacon(void);
void pm_app_beacon_on_wifi(const char* bssid, const char* ssid,
                            int rssi, int channel, const char* enc);
#ifdef __cplusplus
}
#endif
#endif
