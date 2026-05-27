// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_baseball.c — MLB player card browser
//
//  Thin wrapper around pm_ref_browser. Data at /sd/data/baseball/.
//  Gemini fetch path stubbed pending C6 http_post.
// ============================================================

#include "pm_app_baseball.h"
#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

static const char* TAG = "PM_BASEBALL";
static pm_ref_browser_t* s_browser = NULL;
static lv_obj_t* s_screen = NULL;

void pm_app_baseball_fetch(const char* player_name) {
    if (!player_name) return;
    pm_log_w(TAG, "fetch '%s' — needs C6 http_post transport", player_name);
    // TODO when C6 http_post available:
    //   1. POST to statsapi.mlb.com /api/v1/people/search?names=...
    //   2. parse, fetch career stats, build card JSON
    //   3. pm_nosql_write("baseball", id, json, len)
    //   4. browser refresh
}

static void _init(void) {
    pm_ref_config_t cfg = {
        .category    = "baseball",
        .title       = "BASEBALL CARDS",
        .allow_fetch = true,
    };
    s_browser = pm_ref_browser_create(&cfg);
}

static void _enter(void) {
    if (!s_screen && s_browser) {
        pm_ref_browser_refresh(s_browser);
        s_screen = pm_ref_browser_build_screen(s_browser);
    } else if (s_browser) {
        pm_ref_browser_refresh(s_browser);
        pm_ref_browser_sync_ui(s_browser);
    }
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
}

static void _exit_(void)  { pm_log_i(TAG, "exit"); }
static void _deinit(void) {
    if (s_browser) { pm_ref_browser_destroy(s_browser); s_browser = NULL; }
    s_screen = NULL;
}

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
