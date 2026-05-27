// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_ref_surv.c — Survival reference (offline)
//
//  Thin wrapper around pm_ref_browser. Data at /sd/data/survival/.
// ============================================================

#include "pm_app_ref_surv.h"
#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

static const char* TAG = "PM_REF_SURV";
static pm_ref_browser_t* s_browser = NULL;
static lv_obj_t* s_screen = NULL;

static void _init(void) {
    pm_ref_config_t cfg = {
        .category    = "survival",
        .title       = "SURVIVAL REFERENCE",
        .allow_fetch = false,
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
    .id           = "ref_surv",
    .display_name = "REF: SURV",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_ref_surv(void) { return &_APP; }
