// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_wifi.h — WiFi scanner / connect manager
//
//  P4: WiFi radio is on the C6. This app talks to the C6 via
//  pm_c6_bridge with these new commands (TBD in C6 firmware):
//
//    {"cmd":"wifi_scan"}                 →  scan results stream
//    {"cmd":"wifi_connect","ssid":"...","pass":"..."}
//    {"cmd":"wifi_disconnect"}
//    {"cmd":"wifi_status"}
//
//  C6 emits "wifi_scan_result" events (one per AP) plus a final
//  "wifi_scan_done" event. Saved credentials live in pm_nosql
//  category "wifi_keyring".
// ============================================================

#ifndef PM_APP_WIFI_H
#define PM_APP_WIFI_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_wifi(void);

// Bridge dispatch hooks — pm_c6_bridge.c calls these on
// "wifi_scan_result", "wifi_connected", "wifi_disconnected" events.
void pm_app_wifi_on_scan_result(const char* ssid, const char* bssid,
                                  int rssi, int channel, const char* enc);
void pm_app_wifi_on_scan_done(int total);
void pm_app_wifi_on_connected(const char* ssid, const char* ip);
void pm_app_wifi_on_disconnected(void);

#ifdef __cplusplus
}
#endif
#endif
