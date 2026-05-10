// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// pm_app_usb_ducky.h — USB HID injection
// P4 has native USB HID — no C6 needed for this one. Loads a
// Ducky Script .txt and runs it over USB. Press-and-hold start
// guard prevents accidental fires.
#ifndef PM_APP_USB_DUCKY_H
#define PM_APP_USB_DUCKY_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_usb_ducky(void);
#ifdef __cplusplus
}
#endif
#endif
