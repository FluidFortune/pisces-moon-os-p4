// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_system.h — System / diagnostic panel
//
//  Live system stats + actions: reboot, factory reset (NVS
//  erase), SD remount, C6 ping, time sync info, partition
//  table dump.
// ============================================================

#ifndef PM_APP_SYSTEM_H
#define PM_APP_SYSTEM_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_system(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_SYSTEM_H
