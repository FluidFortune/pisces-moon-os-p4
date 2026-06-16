// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_ble_beacon.h — BLE beacon spotter
//
//  Detects and categorizes:
//    - iBeacon       (Apple 0x004C, type 0x02, len 0x15)
//    - AltBeacon     (mfg-spec, 0xBEAC prefix)
//    - Eddystone     (service UUID 0xFEAA, multiple frame types)
//
//  This is the Bluetooth Low Energy beacon spotter. WiFi AP
//  beacons live in a separate app (pm_app_beacon). They share
//  no code: WiFi APs come from esp_wifi scan results, BLE
//  beacons come from GAP advertisement data via main.c.
//
//  ── Hook from main.c ──
//  main.c's GAP callback calls pm_app_ble_beacon_on_adv() once
//  per BLE advertisement with the raw manufacturer-data bytes.
//  The app does its own parsing — keeps the parser concerns
//  out of the boot-time GAP plumbing.
// ============================================================

#ifndef PM_APP_BLE_BEACON_H
#define PM_APP_BLE_BEACON_H

#include "pm_app.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_ble_beacon(void);

// Called by main.c from the BLE GAP scan-result event. mfg_data
// is the raw manufacturer-specific data (after the AD length /
// type bytes) — NOT a hex string. NULL/0 is fine for adverts
// without mfg data.
void pm_app_ble_beacon_on_adv(const char* mac, const char* name,
                                int rssi,
                                const uint8_t* mfg_data, uint8_t mfg_len);

#ifdef __cplusplus
}
#endif

#endif // PM_APP_BLE_BEACON_H
