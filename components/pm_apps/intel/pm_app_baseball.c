// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_baseball.c — Player card browser
//
//  Builds on pm_ref_browser. The fetch path is stubbed;
//  full implementation needs the C6 http_post transport
//  (same dependency as pm_app_terminal).
// ============================================================

#include "pm_app_baseball.h"
#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>

static const char* TAG = "PM_BASEBALL";
static pm_ref_browser_t* s_browser = NULL;

void pm_app_baseball_fetch(const char* player_name) {
    if (!player_name) return;
    pm_log_w(TAG, "fetch '%s' — needs C6 http_post transport", player_name);
    // TODO when C6 http_post available:
    //   1. POST to statsapi.mlb.com /api/v1/people/search?names=...
    //   2. parse, fetch career stats, build card JSON
    //   3. pm_nosql_write("baseball", id, json, len)
    //   4. browser refresh
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BASEBALL",
        "BASEBALL app — UI ready");
}

static void _init(void) {
    pm_ref_config_t cfg = {
        .category    = "baseball",
        .title       = "BASEBALL CARDS",
        .allow_fetch = true,
    };
    s_browser = pm_ref_browser_create(&cfg);
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    if (s_browser) pm_ref_browser_refresh(s_browser);
}

static void _exit_(void)  { pm_log_i(TAG, "exit"); }
static void _deinit(void) { if (s_browser) { pm_ref_browser_destroy(s_browser); s_browser = NULL; } }

static const pm_app_t _APP = {
    .id           = "baseball",
    .display_name = "BASEBALL",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_baseball(void) { return &_APP; }
