// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_filemgr.c — WiFi file manager (HTTP browser)
//
//  Final form on P4:
//    - C6 enters STA/AP mode (special command, not yet
//      implemented in C6 firmware).
//    - P4 stands up esp_http_server bound to whichever
//      interface the C6 routes for it.
//    - HTTP handlers serve /sd via PSRAM-staged reads under
//      the SPI Treaty mutex.
//
//  Current state: scaffold UI, no transport. Pressing START
//  logs "not implemented" and flips a status chip.
// ============================================================

#include "pm_app_filemgr.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"

static const char* TAG = "PM_FILEMGR";

static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_status_chip   = NULL;
static lv_obj_t* s_url_lbl       = NULL;
static lv_obj_t* s_clients_val   = NULL;
static lv_obj_t* s_served_val    = NULL;
static lv_obj_t* s_log_panel     = NULL;
static bool      s_running       = false;

static void _log(const char* line) {
    if (!s_log_panel || !line) return;
    lv_obj_t* l = lv_label_create(s_log_panel);
    lv_label_set_text(l, line);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_font(l, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(l, PM_LAYOUT_COL_FG_DIM, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_scroll_to_y(s_log_panel, LV_COORD_MAX, LV_ANIM_OFF);
}

static void _set_status(const char* text, lv_color_t color) {
    if (!s_status_chip) return;
    lv_label_set_text(s_status_chip, text);
    lv_obj_set_style_text_color(s_status_chip, color, 0);
    lv_obj_t* p = lv_obj_get_parent(s_status_chip);
    if (p) {
        lv_obj_set_style_border_color(p, color, 0);
        lv_obj_set_style_bg_color(p, color, 0);
    }
}

static void _start_cb(lv_event_t* e) {
    (void)e;
    pm_log_w(TAG, "WiFi file manager not yet wired through C6");
    s_running = false;
    _set_status("BLOCKED", PM_LAYOUT_COL_ERR);
    _log("[ERR] WiFi handoff via C6 not yet implemented");
    _log("[INFO] HTTP server will live on /sd once the C6 STA");
    _log("[INFO] mode + inter-chip link land in a later phase.");
}

static void _stop_cb(lv_event_t* e) {
    (void)e;
    s_running = false;
    _set_status("STOPPED", PM_LAYOUT_COL_DIM);
}

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "SD FILES");
    s_status_chip = pm_app_layout_chip(&L, "STOPPED", PM_LAYOUT_COL_DIM);

    pm_app_layout_stats_row(&L, 4);
    s_clients_val = pm_app_layout_stat(&L, "CLIENTS",  "0");
    s_served_val  = pm_app_layout_stat(&L, "REQUESTS", "0");
    pm_app_layout_stat(&L, "BYTES OUT", "0");
    pm_app_layout_stat(&L, "UPLOADS",   "0");

    pm_app_layout_content(&L);

    // Left: server info
    lv_obj_t* left = pm_app_layout_pane(&L, 360, "SERVER");
    lv_obj_t* info = lv_obj_create(left);
    lv_obj_remove_style_all(info);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(info, 14, 0);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info, 8, 0);

    lv_obj_t* url_lbl = lv_label_create(info);
    lv_label_set_text(url_lbl, "URL");
    lv_obj_set_style_text_font(url_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(url_lbl, PM_LAYOUT_COL_DIM, 0);

    s_url_lbl = lv_label_create(info);
    lv_label_set_text(s_url_lbl, "(server not started)");
    lv_obj_set_width(s_url_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_url_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_url_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_label_set_long_mode(s_url_lbl, LV_LABEL_LONG_WRAP);

    lv_obj_t* mode_lbl = lv_label_create(info);
    lv_label_set_text(mode_lbl,
        "Mode: STA (joins your WiFi) or AP (broadcasts SSID).\n"
        "Pending: C6 station / AP routing through ESP-Hosted.\n"
        "Serves /sd read-only when active.");
    lv_obj_set_width(mode_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(mode_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(mode_lbl, PM_LAYOUT_COL_FG_DIM, 0);
    lv_label_set_long_mode(mode_lbl, LV_LABEL_LONG_WRAP);

    // Right: log
    lv_obj_t* right = pm_app_layout_pane(&L, 0, "LOG");
    s_log_panel = lv_obj_create(right);
    lv_obj_remove_style_all(s_log_panel);
    lv_obj_set_width(s_log_panel, LV_PCT(100));
    lv_obj_set_flex_grow(s_log_panel, 1);
    lv_obj_set_style_bg_opa(s_log_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_log_panel, 8, 0);
    lv_obj_set_layout(s_log_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_log_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_log_panel, 2, 0);
    lv_obj_add_flag(s_log_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_log_panel, LV_DIR_VER);

    pm_app_layout_action(&L, "START", PM_LAYOUT_COL_OK,  _start_cb);
    pm_app_layout_action(&L, "STOP",  PM_LAYOUT_COL_ERR, _stop_cb);

    s_screen = pm_app_layout_end(&L);
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
}

static void _exit_(void) { pm_log_i(TAG, "exit"); s_running = false; }

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
    (void)s_running;
    return &_APP;
}
