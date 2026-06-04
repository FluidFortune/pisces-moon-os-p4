// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_pole_position.c — Pole Position (placeholder)
//
//  Launcher tile reserved. Full LVGL port pending; S3
//  implementation lives in pisces-moon-os/src/pole_position.cpp.
// ============================================================

#include "pm_app_pole_position.h"
#include "pm_stub_app.h"
#include "lvgl.h"

static lv_obj_t* s_screen = NULL;

static void _init(void) {
    if (!s_screen) {
        s_screen = pm_stub_app_make_screen(
            "POLE POSITION",
            "ARCADE RACING",
            "First-person racing classic. Steer through a winding track, "
            "hit checkpoints before time expires, miss the curves at your peril.");
    }
}

static void _enter(void) {
    if (!s_screen) _init();
    if (s_screen) lv_screen_load(s_screen);
}

static const pm_app_t _APP = {
    .id           = "pole_position",
    .display_name = "POLE POSITION",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = NULL,
    .deinit       = NULL,
};

const pm_app_t* pm_app_pole_position(void) { return &_APP; }
