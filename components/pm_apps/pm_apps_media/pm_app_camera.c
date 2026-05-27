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
//  If absent, shows "Camera not detected".
// ============================================================

#include "pm_app_camera.h"
#include "pm_camera.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "lvgl.h"
#include <stdio.h>

static const char* TAG = "PM_CAMERA";

static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_viewfinder  = NULL;
static lv_obj_t* s_status_chip = NULL;
static lv_obj_t* s_sensor_chip = NULL;
static lv_obj_t* s_count_val   = NULL;
static lv_obj_t* s_res_val     = NULL;
static lv_obj_t* s_msg_lbl     = NULL;
static int       s_counter     = 0;

static void _snap_cb(lv_event_t* e) {
    (void)e;
    if (!pm_camera_present()) {
        if (s_msg_lbl) lv_label_set_text(s_msg_lbl, "No camera attached.");
        return;
    }
    char name[40];
    snprintf(name, sizeof(name), "snap_%lu.jpg", (unsigned long)pm_millis());
    pm_camera_snapshot_to_sd(name);
    s_counter++;
    if (s_count_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_counter);
        lv_label_set_text(s_count_val, buf);
    }
    if (s_msg_lbl) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Saved %s to /sd/photos/", name);
        lv_label_set_text(s_msg_lbl, buf);
    }
}

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "CAMERA");
    s_status_chip = pm_app_layout_chip(&L, "DETECTING", PM_LAYOUT_COL_WARN);
    s_sensor_chip = pm_app_layout_chip(&L, pm_camera_sensor_name(),
                                         PM_LAYOUT_COL_ACCENT);

    pm_app_layout_stats_row(&L, 3);
    s_count_val = pm_app_layout_stat(&L, "CAPTURED", "0");
    s_res_val   = pm_app_layout_stat(&L, "RESOLUTION", "640x480");
    pm_app_layout_stat(&L, "FORMAT", "JPEG");

    pm_app_layout_content(&L);

    // Left pane: viewfinder canvas
    lv_obj_t* left = pm_app_layout_pane(&L, 0, "VIEWFINDER");
    s_viewfinder = lv_obj_create(left);
    lv_obj_remove_style_all(s_viewfinder);
    lv_obj_set_width(s_viewfinder, LV_PCT(100));
    lv_obj_set_flex_grow(s_viewfinder, 1);
    lv_obj_set_style_bg_color(s_viewfinder, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(s_viewfinder, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_viewfinder, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* center = lv_label_create(s_viewfinder);
    lv_label_set_text(center, "(no signal)");
    lv_obj_set_style_text_font(center, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(center, PM_LAYOUT_COL_DIM, 0);
    lv_obj_center(center);
    s_msg_lbl = center;

    // Right pane: info / hints
    lv_obj_t* right = pm_app_layout_pane(&L, 280, "STATUS");
    lv_obj_t* info = lv_obj_create(right);
    lv_obj_remove_style_all(info);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(info, 12, 0);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info, 6, 0);

    lv_obj_t* hint = lv_label_create(info);
    lv_label_set_text(hint,
        "CSI camera plugs into the CIS-CAM connector.\n"
        "Snapshots save to /sd/photos/.\n"
        "Set resolution via pm_camera API.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hint, PM_LAYOUT_COL_FG_DIM, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    pm_app_layout_action(&L, "SNAP",   PM_LAYOUT_COL_OK,     _snap_cb);

    s_screen = pm_app_layout_end(&L);
}

static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");

    bool present = pm_camera_present();
    if (s_status_chip) {
        lv_label_set_text(s_status_chip, present ? "STREAMING" : "NO CAMERA");
        lv_color_t c = present ? PM_LAYOUT_COL_OK : PM_LAYOUT_COL_ERR;
        lv_obj_set_style_text_color(s_status_chip, c, 0);
        lv_obj_t* chip_parent = lv_obj_get_parent(s_status_chip);
        if (chip_parent) {
            lv_obj_set_style_border_color(chip_parent, c, 0);
            lv_obj_set_style_bg_color(chip_parent, c, 0);
        }
    }
    if (present) {
        pm_camera_stream_start(640, 480, PM_CAM_PIXFMT_RGB565, NULL, NULL);
        if (s_msg_lbl) lv_label_set_text(s_msg_lbl,
            "Viewfinder active.\nTap SNAP to save a photo.");
    } else {
        if (s_msg_lbl) lv_label_set_text(s_msg_lbl,
            "Camera not detected.\nPlug the CSI ribbon into\nthe CIS-CAM connector.");
    }
}

static void _exit_(void) {
    if (pm_camera_present()) pm_camera_stream_stop();
    pm_log_i(TAG, "exit");
}

static const pm_app_t _APP = {
    .id = "camera", .display_name = "CAMERA",
    .category = PM_CAT_MEDIA, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_camera(void) { return &_APP; }
