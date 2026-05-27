// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_wpa_hs.h — WPA handshake collector
// Consumer of C6 "handshake" events (when C6 fw supports msg1+msg2
// EAPOL capture). Saves hashcat .hccapx files to /sd/handshakes/.
// Display shows captured handshake count + most recent SSID.
#ifndef PM_APP_WPA_HS_H
#define PM_APP_WPA_HS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_wpa_hs(void);
void pm_app_wpa_hs_on_capture(const char* ssid, const char* bssid,
                                const char* hccapx_b64);
#ifdef __cplusplus
}
#endif
#endif
