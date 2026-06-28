// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_wardrive.h — Session-aware wardrive viewer + logger
//
//  P4 architecture: the Ghost Engine *always* runs on the C6.
//  This app is the P4-side viewer + per-session SQLite logger.
//
//  Session DB: /sd/sessions/session_<ts>.db
//  Schema:     pm_wardrive_schema.h
//
//  Inserts are upserts on (bssid) for wifi, (mac) for ble —
//  duplicates accumulate hits, refresh rssi/last_ms, leave
//  first_ms intact.
//
//  CSV export: writes a Jennifer-compatible
//    /sd/exports/wardrive_<ts>.csv
//  with the legacy column shape so existing Jennifer pipelines
//  consume it unchanged.
//
//  Fallback: if SQLite is misbehaving, set
//    pm_app_wardrive_use_csv_fallback(true) to switch to direct
//    CSV append. Same column order as export.
// ============================================================

#ifndef PM_APP_WARDRIVE_H
#define PM_APP_WARDRIVE_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_wardrive(void);

// Subscribers — main.c routes C6 events here too.
void pm_app_wardrive_on_wifi(const char* bssid, const char* ssid,
                              int rssi, int channel, const char* enc);
void pm_app_wardrive_on_ble (const char* mac, const char* name,
                              int rssi, const char* addr_type,
                              const char* mfg);
void pm_app_wardrive_on_probe(const char* mac, const char* ssid,
                                int rssi, int count);
void pm_app_wardrive_on_pkt (const char* frame_type, const char* src,
                              int rssi);

// LoRa intake. Buf is the raw radio frame as delivered by pm_lora
// (post-FEC, pre-protocol-parse). The wardrive logger attempts a
// best-effort Meshtastic header parse; frames that aren't
// recognizable still get logged with their RF metadata so the map
// shows "something out there" markers.
void pm_app_wardrive_on_lora(const uint8_t* buf, size_t len,
                              int rssi, float snr,
                              uint32_t freq_khz, const char* preset);

void pm_app_wardrive_use_csv_fallback(bool on);
bool pm_app_wardrive_export_csv(void);     // writes current session to CSV

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_WARDRIVE_H
