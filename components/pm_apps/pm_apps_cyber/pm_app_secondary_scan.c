// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_secondary_scan.c — parallel WiFi scan via T-Beam
//
//  Demonstrates the modular peer model: while the C6 Ghost
//  Engine wardrives in the background (continuous), the user
//  can launch an explicit, scoped scan on the T-Beam Supreme
//  S3 secondary radio. Useful for:
//
//    - Probing a single AP while keeping wardrive logs intact
//    - Active scan with directed dwell times
//    - WiFi scan during MITM or active engagement use
//
//  If no T-Beam is attached, app shows a clear message:
//  "Secondary radio peer not present (modular: attach T-Beam
//  Supreme S3 to the 2x12 header to enable)."
// ============================================================

#include "pm_app_secondary_scan.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "lvgl.h"

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_count_lbl;
static int       s_seen = 0;

static void _refresh_status(void) {
    pm_peer_t* p = pm_peer_find("wifi_scan", PM_PEER_ROLE_SECONDARY);
    if (s_status_lbl) {
        lv_label_set_text(s_status_lbl,
            p ? "T-Beam secondary radio ready"
              : "No secondary radio peer (attach T-Beam)");
    }
}

static void _scan_cb(lv_event_t* e) {
    (void)e;
    pm_peer_t* p = pm_peer_find("wifi_scan", PM_PEER_ROLE_SECONDARY);
    if (!p) { _refresh_status(); return; }
    s_seen = 0;
    pm_peer_call(p, "wifi_scan", NULL);
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Scanning…");
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "SECONDARY SCAN", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl = pm_ui_kv_row(card, "Status",  "checking…");
    s_count_lbl  = pm_ui_kv_row(card, "Networks seen", "0");
    pm_ui_button(s_screen, "SCAN NOW", _scan_cb, NULL);

    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint,
        "Active scan on the SECONDARY WiFi peer (T-Beam Supreme S3).\n"
        "The PRIMARY C6 wardrive continues uninterrupted.");
    lv_obj_set_style_text_color(hint, PM_C_FG_DIM, 0);
}

static void _init (void) { _build_screen(); }
static void _enter(void) { if (s_screen) lv_screen_load(s_screen); _refresh_status(); }
static void _exit_(void) { }

static const pm_app_t _APP = {
    .id = "secondary_scan", .display_name = "2ND SCAN",
    .category = PM_CAT_CYBER, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_secondary_scan(void) { return &_APP; }
