// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_elf_browser.h — ELF module browser
//
//  Lists *.elf files in /sd/elf/, displays each module's
//  manifest (if a sibling .json is present), and launches
//  selected modules through pm_elf_loader (RISC-V port —
//  Phase 2 stub; the loader itself is a separate component).
//
//  Manifest format (carried over from S3):
//    {
//      "name":   "rust_demo",
//      "abi":    1,
//      "psram_required_kb": 256,
//      "category": "ELF"
//    }
// ============================================================

#ifndef PM_APP_ELF_BROWSER_H
#define PM_APP_ELF_BROWSER_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_elf_browser(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_ELF_BROWSER_H
