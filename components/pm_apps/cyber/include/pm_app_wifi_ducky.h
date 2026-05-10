// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// pm_app_wifi_ducky.h — WiFi remote ducky
// Hosts a small captive AP (via C6) with a web form. Whatever
// the operator types in the form is replayed as HID keystrokes
// over USB. Pairs with the USB ducky's HID layer.
#ifndef PM_APP_WIFI_DUCKY_H
#define PM_APP_WIFI_DUCKY_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_wifi_ducky(void);
#ifdef __cplusplus
}
#endif
#endif
