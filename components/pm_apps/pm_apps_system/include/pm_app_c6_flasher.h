// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_c6_flasher.h — System / Ghost Engine flasher
//
//  Reflashes the onboard ESP32-C6 from a firmware blob on the
//  SD card. Uses pm_c6_programmer for the bootloader protocol.
//
//  UI flow:
//    1. List .bin files under /sd/ghost/
//    2. User taps a file
//    3. Confirm dialog
//    4. Progress screen (RESET → SYNC → ERASE → WRITE → DONE)
//    5. On done: option to reboot the C6 into new firmware
// ============================================================

#ifndef PM_APP_C6_FLASHER_H
#define PM_APP_C6_FLASHER_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_c6_flasher(void);

#ifdef __cplusplus
}
#endif

#endif
