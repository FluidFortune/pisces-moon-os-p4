// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_tetris.c — Tetris (placeholder)
//
//  Launcher tile reserved. Full LVGL port pending; S3
//  implementation lives in pisces-moon-os/src/tetris.cpp.
// ============================================================

#include "pm_app_tetris.h"
#include "pm_stub_app.h"
#include "lvgl.h"

static lv_obj_t* s_screen = NULL;

static void _init(void) {
    if (!s_screen) {
        s_screen = pm_stub_app_make_screen(
            "TETRIS",
            "FALLING BLOCK PUZZLE",
            "Rotate and stack tetrominoes as they fall. Clear filled rows. "
            "Speed climbs as you level up.");
    }
}

static void _enter(void) {
    if (!s_screen) _init();
    if (s_screen) lv_screen_load(s_screen);
}

static const pm_app_t _APP = {
    .id           = "tetris",
    .display_name = "TETRIS",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = NULL,
    .deinit       = NULL,
};

const pm_app_t* pm_app_tetris(void) { return &_APP; }
