// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// pm_app_ble_ducky.h — BLE HID injection
// P4 HID over the C6's BLE stack. Loads a Ducky Script .txt
// from SD, sends it as a series of HID keyboard events to a
// paired host. Useful for HID payload demos / testing.
#ifndef PM_APP_BLE_DUCKY_H
#define PM_APP_BLE_DUCKY_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_ble_ducky(void);
#ifdef __cplusplus
}
#endif
#endif
