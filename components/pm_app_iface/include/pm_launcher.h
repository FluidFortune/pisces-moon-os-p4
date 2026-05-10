// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_launcher.h — LVGL launcher / category grid
//
//  Pisces Moon P4 launcher displays the seven category tiles,
//  drilling into each to show the apps registered in it.
//
//  Categories (matches S3 launcher layout):
//    COMMS, CYBER, TOOLS, GAMES, INTEL, MEDIA, SYSTEM
//
//  Resolution: 1024×600 — tiles are roomy compared to S3
//  (320×240). Layout is a 4-column grid for category view,
//  then a paged 4-column app grid inside each category.
// ============================================================

#ifndef PM_LAUNCHER_H
#define PM_LAUNCHER_H

#include <stdbool.h>
#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Boot / lifecycle ─────────────────────────────────────────
void pm_launcher_init(void);            // Build LVGL screen
void pm_launcher_show(void);            // Show launcher (back from app)
void pm_launcher_hide(void);            // Hide launcher (entering app)

// ── Navigation ───────────────────────────────────────────────
typedef enum {
    PM_LAUNCHER_VIEW_CATEGORIES,
    PM_LAUNCHER_VIEW_APPS,
} pm_launcher_view_t;

pm_launcher_view_t pm_launcher_current_view(void);
void pm_launcher_open_category(pm_category_t cat);
void pm_launcher_back_to_categories(void);
void pm_launcher_open_app(const pm_app_t* app);
void pm_launcher_back_from_app(void);   // Called when user backs out of an app

// ── Category metadata (display name, accent color) ───────────
typedef struct {
    const char* name;       // "COMMS", "CYBER", ...
    const char* short_id;   // "C", "X", ...
    uint32_t    accent_rgb; // 0xRRGGBB for tile chamfer / app grid accent
} pm_category_meta_t;

const pm_category_meta_t* pm_category_meta(pm_category_t cat);

#ifdef __cplusplus
}
#endif

#endif  // PM_LAUNCHER_H
