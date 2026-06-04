// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_breakout.c — Breakout (placeholder)
//
//  Launcher tile reserved. Full LVGL port pending; S3
//  implementation lives in pisces-moon-os/src/breakout.cpp.
// ============================================================

#include "pm_app_breakout.h"
#include "pm_stub_app.h"
#include "lvgl.h"

static lv_obj_t* s_screen = NULL;

static void _init(void) {
    if (!s_screen) {
        s_screen = pm_stub_app_make_screen(
            "BREAKOUT",
            "BRICK BREAKER",
            "Bounce the ball off your paddle to clear every brick on the screen. "
            "Don't let the ball drop past you.");
    }
}

static void _enter(void) {
    if (!s_screen) _init();
    if (s_screen) lv_screen_load(s_screen);
}

static const pm_app_t _APP = {
    .id           = "breakout",
    .display_name = "BREAKOUT",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = NULL,
    .deinit       = NULL,
};

const pm_app_t* pm_app_breakout(void) { return &_APP; }
