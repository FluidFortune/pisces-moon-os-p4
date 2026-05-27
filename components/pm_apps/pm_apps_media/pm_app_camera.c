// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_camera.c — Viewfinder + snapshot
//
//  If pm_camera_present(), starts a viewfinder stream and shows
//  it on a canvas. Tap SNAP to save a JPEG to /sd/photos/.
//  If absent, shows "Camera not detected (modular: plug CSI
//  ribbon into CIS-CAM connector)".
// ============================================================

#include "pm_app_camera.h"
#include "pm_camera.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_canvas;
static int       s_counter = 0;

static void _snap_cb(lv_event_t* e) {
    (void)e;
    if (!pm_camera_present()) return;
    char name[40];
    snprintf(name, sizeof(name), "snap_%lu.jpg",
             (unsigned long)pm_millis());
    pm_camera_snapshot_to_sd(name);
    s_counter++;
    if (s_status_lbl) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Saved %d photo(s)", s_counter);
        lv_label_set_text(s_status_lbl, buf);
    }
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "CAMERA", NULL, NULL);

    s_canvas = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_size(s_canvas, LV_PCT(100), 400);
    lv_obj_set_style_bg_color(s_canvas, PM_C_BG_3, 0);
    lv_obj_set_style_bg_opa  (s_canvas, LV_OPA_COVER, 0);

    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl = pm_ui_kv_row(card, "Status", "checking…");
    pm_ui_kv_row(card, "Sensor", pm_camera_sensor_name());

    pm_ui_button(s_screen, "SNAP", _snap_cb, NULL);
}

static void _init (void) { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_status_lbl) {
        lv_label_set_text(s_status_lbl,
            pm_camera_present() ? "Streaming…"
                                : "Camera not detected");
    }
    if (pm_camera_present()) {
        pm_camera_stream_start(640, 480, PM_CAM_PIXFMT_RGB565,
                                NULL, NULL);
    }
}
static void _exit_(void) { if (pm_camera_present()) pm_camera_stream_stop(); }

static const pm_app_t _APP = {
    .id = "camera", .display_name = "CAMERA",
    .category = PM_CAT_MEDIA, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_camera(void) { return &_APP; }
