// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#include "pm_app_trails.h"
#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

static const char* TAG = "PM_TRAILS";
static pm_ref_browser_t* s_browser = NULL;

void pm_app_trails_fetch(const char* name_and_region) {
    if (!name_and_region) return;
    pm_log_w(TAG, "fetch '%s' — needs C6 http_post transport", name_and_region);
    // Same shape as pm_app_baseball_fetch — Gemini structured JSON request
    // through C6 once http_post lands.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("TRAILS",
        "TRAILS app — UI ready");
}

static void _init(void) {
    pm_ref_config_t cfg = {
        .category    = "trails",
        .title       = "HIKING TRAILS",
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
    .id           = "trails",
    .display_name = "TRAILS",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_trails(void) { return &_APP; }
