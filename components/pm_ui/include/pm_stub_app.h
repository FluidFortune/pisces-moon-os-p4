// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_stub_app.h — Shared "coming soon" screen builder
//
//  Apps that are registered for launcher coverage but whose
//  full implementation is still pending use this helper to
//  produce a consistent placeholder screen:
//
//    cyberpunk PCB background
//    rainbow title
//    one-line genre / source line
//    descriptive paragraph
//    "PORT IN PROGRESS" status chip
//    BACK action wired to the launcher
//
//  This keeps the launcher honest — every tile opens to a
//  coherent screen, and the placeholder is visually different
//  enough from a real app that users (and us) know what's
//  still pending.
// ============================================================

#ifndef PM_STUB_APP_H
#define PM_STUB_APP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build and return a placeholder screen. The screen is fully
// independent — no shared state with other stubs — so multiple
// stub apps can co-exist.
lv_obj_t* pm_stub_app_make_screen(const char* title,
                                    const char* tagline,
                                    const char* description);

#ifdef __cplusplus
}
#endif
#endif
