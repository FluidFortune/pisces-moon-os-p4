// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_nfc_clone.c — Read source tag, save to SD, write blank
//
//  Workflow:
//    1. User taps "READ" with source tag in field — UID + blocks
//       saved to /sd/nfc/<uid>.bin
//    2. User taps "WRITE" with blank tag in field — block-by-block
//       writeback.
//
//  Legal disclaimer in-app: clone only tags you own or have
//  written authorization to duplicate.
// ============================================================

#ifndef PM_APP_NFC_CLONE_H
#define PM_APP_NFC_CLONE_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_nfc_clone(void);
#ifdef __cplusplus
}
#endif
#endif
