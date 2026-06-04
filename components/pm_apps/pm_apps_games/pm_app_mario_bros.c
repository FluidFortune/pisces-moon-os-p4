// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_mario_bros.c — Mario Bros (placeholder)
//
//  Launcher tile reserved. Full LVGL port pending; S3
//  implementation lives in pisces-moon-os/src/mario_bros.cpp.
// ============================================================

#include "pm_app_mario_bros.h"
#include "pm_stub_app.h"
#include "lvgl.h"

static lv_obj_t* s_screen = NULL;

static void _init(void) {
    if (!s_screen) {
        s_screen = pm_stub_app_make_screen(
            "MARIO BROS",
            "PLATFORMER",
            "Run, jump, and stomp through the Mushroom Kingdom. Collect coins, "
            "knock out goombas, find the flagpole at the end of every level.");
    }
}

static void _enter(void) {
    if (!s_screen) _init();
    if (s_screen) lv_screen_load(s_screen);
}

static const pm_app_t _APP = {
    .id           = "mario_bros",
    .display_name = "MARIO BROS",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = NULL,
    .deinit       = NULL,
};

const pm_app_t* pm_app_mario_bros(void) { return &_APP; }
