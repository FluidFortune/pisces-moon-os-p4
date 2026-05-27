// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_camera_qr.c — QR / barcode scanner
//
//  Uses pm_camera grayscale frames + quirc decoder (planned).
//  For the alpha, scaffold the app surface; quirc integration
//  is hardware-bring-up work.
// ============================================================

#include "pm_app_camera_qr.h"
#include "pm_camera.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "lvgl.h"
#include <stdio.h>

static const char* TAG = "PM_QR";

static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_status_chip = NULL;
static lv_obj_t* s_decoded_lbl = NULL;
static lv_obj_t* s_history_lst = NULL;
static lv_obj_t* s_count_val   = NULL;
static int       s_decoded_count = 0;

static void _clear_history_cb(lv_event_t* e) {
    (void)e;
    if (s_history_lst) lv_obj_clean(s_history_lst);
    s_decoded_count = 0;
    if (s_count_val) lv_label_set_text(s_count_val, "0");
    if (s_decoded_lbl) lv_label_set_text(s_decoded_lbl,
        "Aim the camera at a code...");
}

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "QR SCAN");
    s_status_chip = pm_app_layout_chip(&L, "DETECTING", PM_LAYOUT_COL_WARN);

    pm_app_layout_stats_row(&L, 3);
    s_count_val = pm_app_layout_stat(&L, "DECODED", "0");
    pm_app_layout_stat(&L, "FORMAT", "QR / 1D");
    pm_app_layout_stat(&L, "FPS", "—");

    pm_app_layout_content(&L);

    // Left: viewfinder placeholder
    lv_obj_t* left = pm_app_layout_pane(&L, 0, "VIEWFINDER");
    lv_obj_t* viewfinder = lv_obj_create(left);
    lv_obj_remove_style_all(viewfinder);
    lv_obj_set_width(viewfinder, LV_PCT(100));
    lv_obj_set_flex_grow(viewfinder, 1);
    lv_obj_set_style_bg_color(viewfinder, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(viewfinder, LV_OPA_COVER, 0);
    lv_obj_clear_flag(viewfinder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* msg = lv_label_create(viewfinder);
    lv_label_set_text(msg,
        "Aim the camera at a QR or barcode.\nDecoded content appears at right.");
    lv_obj_set_style_text_font(msg, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(msg, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(msg);

    // Right: decoded + history
    lv_obj_t* right = pm_app_layout_pane(&L, 360, "DECODED");
    lv_obj_t* col = lv_obj_create(right);
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(col, 8, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(col, 8, 0);

    s_decoded_lbl = lv_label_create(col);
    lv_label_set_text(s_decoded_lbl,
        pm_camera_present() ? "Waiting for code..." : "Camera not detected.");
    lv_obj_set_width(s_decoded_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_decoded_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_decoded_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_label_set_long_mode(s_decoded_lbl, LV_LABEL_LONG_WRAP);

    lv_obj_t* hist_hdr = lv_label_create(col);
    lv_label_set_text(hist_hdr, "HISTORY");
    lv_obj_set_style_text_font(hist_hdr, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hist_hdr, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(hist_hdr, 1, 0);

    s_history_lst = lv_obj_create(col);
    lv_obj_remove_style_all(s_history_lst);
    lv_obj_set_width(s_history_lst, LV_PCT(100));
    lv_obj_set_flex_grow(s_history_lst, 1);
    lv_obj_set_style_bg_color(s_history_lst, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(s_history_lst, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_history_lst, 4, 0);
    lv_obj_set_style_pad_all(s_history_lst, 4, 0);
    lv_obj_set_layout(s_history_lst, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_history_lst, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_history_lst, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_history_lst, LV_DIR_VER);

    pm_app_layout_action(&L, "CLEAR HISTORY", PM_LAYOUT_COL_ERR,
                         _clear_history_cb);

    s_screen = pm_app_layout_end(&L);
}

// Public API for the quirc decoder (when integrated) to push results
void pm_app_camera_qr_on_decoded(const char* text) {
    if (!text || !text[0] || !s_history_lst) return;
    s_decoded_count++;
    if (s_count_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_decoded_count);
        lv_label_set_text(s_count_val, buf);
    }
    if (s_decoded_lbl) lv_label_set_text(s_decoded_lbl, text);

    lv_obj_t* row = lv_label_create(s_history_lst);
    lv_label_set_text(row, text);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_text_font(row, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(row, PM_LAYOUT_COL_FG, 0);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
}

static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");

    bool present = pm_camera_present();
    if (s_status_chip) {
        lv_label_set_text(s_status_chip, present ? "SCANNING" : "NO CAMERA");
        lv_color_t c = present ? PM_LAYOUT_COL_OK : PM_LAYOUT_COL_ERR;
        lv_obj_set_style_text_color(s_status_chip, c, 0);
        lv_obj_t* p = lv_obj_get_parent(s_status_chip);
        if (p) {
            lv_obj_set_style_border_color(p, c, 0);
            lv_obj_set_style_bg_color(p, c, 0);
        }
    }
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id = "camera_qr", .display_name = "QR SCAN",
    .category = PM_CAT_TOOLS, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_camera_qr(void) { return &_APP; }
