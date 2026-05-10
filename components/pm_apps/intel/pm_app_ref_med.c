// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_ref_med.c — Medical reference (offline)
//
//  Thin wrapper around pm_ref_browser. Data lives in
//  /sd/data/medical/ — pre-populate on host with an
//  index.json + entry_NNN.json files.
// ============================================================

#include "pm_app_ref_med.h"
#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

static const char* TAG = "PM_REF_MED";
static pm_ref_browser_t* s_browser = NULL;

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("REF: MED",
        "REF: MED app — UI ready");
}

static void _init(void) {
    pm_ref_config_t cfg = {
        .category    = "medical",
        .title       = "MEDICAL REFERENCE",
        .allow_fetch = false,        // Offline only — no Gemini fallback
    };
    s_browser = pm_ref_browser_create(&cfg);
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    if (s_browser) pm_ref_browser_refresh(s_browser);
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_browser) { pm_ref_browser_destroy(s_browser); s_browser = NULL; }
}

static const pm_app_t _APP = {
    .id           = "ref_med",
    .display_name = "REF: MED",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_ref_med(void) { return &_APP; }
