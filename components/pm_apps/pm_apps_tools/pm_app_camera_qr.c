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
#include "lvgl.h"

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_decoded_lbl;

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "QR SCAN", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl  = pm_ui_kv_row(card, "Status",   "checking…");
    s_decoded_lbl = pm_ui_kv_row(card, "Decoded",  "—");

    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint,
        "Aim the camera at a QR code or barcode.\n"
        "Decoded contents appear above.");
    lv_obj_set_style_text_color(hint, PM_C_FG_DIM, 0);
}

static void _init (void) { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_status_lbl) lv_label_set_text(s_status_lbl,
        pm_camera_present() ? "Decoding…" : "Camera not detected");
}
static void _exit_(void) { }

static const pm_app_t _APP = {
    .id = "camera_qr", .display_name = "QR SCAN",
    .category = PM_CAT_TOOLS, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_camera_qr(void) { return &_APP; }
