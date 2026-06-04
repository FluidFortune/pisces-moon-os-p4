// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_frogger.c — Frogger (placeholder)
//
//  Launcher tile reserved. Full LVGL port pending; S3
//  implementation lives in pisces-moon-os/src/frogger.cpp.
// ============================================================

#include "pm_app_frogger.h"
#include "pm_stub_app.h"
#include "lvgl.h"

static lv_obj_t* s_screen = NULL;

static void _init(void) {
    if (!s_screen) {
        s_screen = pm_stub_app_make_screen(
            "FROGGER",
            "ARCADE CLASSIC",
            "Cross the highway dodging traffic, then the river hopping logs "
            "and turtles. Reach the safe lily pads at the top.");
    }
}

static void _enter(void) {
    if (!s_screen) _init();
    if (s_screen) lv_screen_load(s_screen);
}

static const pm_app_t _APP = {
    .id           = "frogger",
    .display_name = "FROGGER",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = NULL,
    .deinit       = NULL,
};

const pm_app_t* pm_app_frogger(void) { return &_APP; }
