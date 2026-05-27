// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_filemgr.c — WiFi file manager (Phase 2 stub)
//
//  Final form on P4:
//    - C6 enters STA/AP mode (special command, not yet
//      implemented in C6 firmware).
//    - P4 stands up esp_http_server bound to whichever
//      interface the C6 routes for it (TBD: USB-NCM tunnel
//      via the inter-chip link, or a bridge on the P4).
//    - HTTP handlers serve /sd via PSRAM-staged reads under
//      the SPI Treaty mutex.
//
//  Phase 2 stub: status panel only. Pressing "START" logs
//  the intent and shows a "not implemented" message.
// ============================================================

#include "pm_app_filemgr.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

static const char* TAG = "PM_FILEMGR";

static bool s_running = false;

static void _action_start(void) {
    pm_log_w(TAG, "WiFi file manager not yet wired through C6 — stub only");
    s_running = false;
}

static void _action_stop(void) {
    s_running = false;
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("SD FILES",
        "SD FILES app — UI ready");
}

static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) { pm_log_i(TAG, "exit");  s_running = false; }

static const pm_app_t _APP = {
    .id           = "filemgr",
    .display_name = "SD FILES",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_filemgr(void) {
    (void)_action_start; (void)_action_stop;  // wired by LVGL once UI is real
    return &_APP;
}
