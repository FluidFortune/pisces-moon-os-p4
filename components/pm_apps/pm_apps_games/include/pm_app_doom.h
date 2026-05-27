// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_doom.h — DOOM
//
//  The S3 build used a custom Arduino-port of Doom that loaded
//  a WAD from internal flash (gamedata partition). For P4:
//    - WAD lives at /gamedata/doom.wad (still partition-backed)
//    - Renderer uses an LVGL canvas at native resolution
//    - PSRAM lifts most engine sub-allocs (32MB > 8MB on S3)
//
//  This is a scaffold. The actual Doom engine port (RISC-V,
//  ESP-IDF, framebuffer to LVGL) is a project unto itself —
//  the architecture is here, the engine is a TODO.
// ============================================================

#ifndef PM_APP_DOOM_H
#define PM_APP_DOOM_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_doom(void);
#ifdef __cplusplus
}
#endif
#endif
