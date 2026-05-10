// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gps.c — Live GPS readout
//
//  Renders pm_gps_state. P4 has way more screen than the S3
//  T-Deck — the layout is:
//    Header: status + GPS fix indicator
//    Big lat/lng readout
//    Altitude / speed / sat count side panel
//    "Last update Ns ago" row
//    [COPY COORDS] button (writes lat,lng to NoSQL clipboard)
// ============================================================

#include "pm_app_gps.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_gps_state.h"
#include "pm_nosql.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_GPS";

// LVGL
static void* s_screen     = NULL;
static void* s_lbl_status = NULL;
static void* s_lbl_lat    = NULL;
static void* s_lbl_lng    = NULL;
static void* s_lbl_alt    = NULL;
static void* s_lbl_speed  = NULL;
static void* s_lbl_sats   = NULL;
static void* s_lbl_age    = NULL;

static uint32_t s_last_render_ms = 0;

static void _render(void) {
    pm_gps_t g;
    pm_gps_state_get(&g);

    char buf[80];
    if (g.last_update_ms == 0) {
        snprintf(buf, sizeof(buf), "STATUS: waiting for first GPS event");
    } else if (g.valid) {
        snprintf(buf, sizeof(buf), "STATUS: 3D FIX  |  sats %d", g.sats);
    } else {
        snprintf(buf, sizeof(buf), "STATUS: NO FIX  |  sats %d", g.sats);
    }
    // TODO_LVGL: lv_label_set_text(s_lbl_status, buf);

    snprintf(buf, sizeof(buf), "LAT  %+10.6f", g.lat);
    // TODO_LVGL: lv_label_set_text(s_lbl_lat, buf);
    snprintf(buf, sizeof(buf), "LNG  %+11.6f", g.lng);
    // TODO_LVGL: lv_label_set_text(s_lbl_lng, buf);

    snprintf(buf, sizeof(buf), "ALT %.1f m", g.alt_m);
    // TODO_LVGL: lv_label_set_text(s_lbl_alt, buf);
    snprintf(buf, sizeof(buf), "SPD %.1f m/s", g.speed_mps);
    // TODO_LVGL: lv_label_set_text(s_lbl_speed, buf);
    snprintf(buf, sizeof(buf), "SAT %d", g.sats);
    // TODO_LVGL: lv_label_set_text(s_lbl_sats, buf);

    if (g.last_update_ms == 0) {
        snprintf(buf, sizeof(buf), "no events received yet");
    } else {
        uint32_t age = pm_millis() - g.last_update_ms;
        snprintf(buf, sizeof(buf), "last update %u ms ago", (unsigned)age);
    }
    // TODO_LVGL: lv_label_set_text(s_lbl_age, buf);
    (void)buf;
}

static void _action_copy_coords(void) {
    pm_gps_t g;
    pm_gps_state_get(&g);
    if (!g.valid) return;
    char line[64];
    int n = snprintf(line, sizeof(line), "%.6f,%.6f", g.lat, g.lng);
    pm_nosql_write("clipboard", "coords", line, (size_t)n);
    pm_log_i(TAG, "copied: %s", line);
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("GPS",
        "GPS app — UI ready");
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_last_render_ms = 0;
    _render();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 250) return;
    s_last_render_ms = now;
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "gps",
    .display_name = "GPS",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_gps(void) {
    (void)_action_copy_coords;
    return &_APP;
}
