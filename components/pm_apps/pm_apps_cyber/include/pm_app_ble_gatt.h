// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_ble_gatt.h — GATT service explorer
// Connects to a BLE peer (via C6 ble_connect command) and
// enumerates services / characteristics / descriptors. Read
// values, decode common service UUIDs, write to writable chars.
#ifndef PM_APP_BLE_GATT_H
#define PM_APP_BLE_GATT_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_ble_gatt(void);
void pm_app_ble_gatt_on_service(const char* uuid, int handle_start, int handle_end);
void pm_app_ble_gatt_on_char(const char* uuid, int handle, const char* props);
void pm_app_ble_gatt_on_value(int handle, const char* hex);
#ifdef __cplusplus
}
#endif
#endif
