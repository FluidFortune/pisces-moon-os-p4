// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_gemini_log.c — Saved chat browser
//
//  Two-pane: session list on left, session contents on right.
//  Sessions stored under nosql category "gemini_log".
// ============================================================

#include "pm_app_gemini_log.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_GEMLOG";

#define MAX_SESSIONS 64
#define ID_SIZE      40
#define SESSION_BUF_SZ (128 * 1024)

static char   s_ids[MAX_SESSIONS * ID_SIZE];
static int    s_count = 0;
static char   s_open_id[ID_SIZE] = "";
static char*  s_session_buf = NULL;

// LVGL handles
static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_list_panel  = NULL;
static lv_obj_t* s_viewer_lbl  = NULL;
static lv_obj_t* s_count_chip  = NULL;

static const char* _id_at(int i) {
    if (i < 0 || i >= s_count) return "";
    return &s_ids[i * ID_SIZE];
}

static void _row_clicked(lv_event_t* e) {
    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= s_count) return;
    if (!s_session_buf) {
        s_session_buf = (char*)pm_psram_alloc(SESSION_BUF_SZ);
        if (!s_session_buf) return;
    }
    const char* id = _id_at(idx);
    strncpy(s_open_id, id, sizeof(s_open_id) - 1);
    s_open_id[sizeof(s_open_id) - 1] = 0;
    size_t got = pm_nosql_read("gemini_log", id,
                                s_session_buf, SESSION_BUF_SZ - 1);
    s_session_buf[got] = 0;
    pm_log_i(TAG, "loaded session '%s' (%zu bytes)", id, got);
    if (s_viewer_lbl) {
        lv_label_set_text(s_viewer_lbl,
            got > 0 ? s_session_buf : "(empty session)");
    }
}

static void _populate_list(void) {
    if (!s_list_panel) return;
    lv_obj_clean(s_list_panel);

    if (s_count == 0) {
        lv_obj_t* empty = lv_label_create(s_list_panel);
        lv_label_set_text(empty,
            "No saved sessions.\nUse Gemini Terminal to save chats.");
        lv_obj_set_style_text_font(empty, PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(empty, PM_LAYOUT_COL_DIM, 0);
        lv_obj_set_style_pad_all(empty, 12, 0);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        lv_obj_t* row = lv_obj_create(s_list_panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, PM_LAYOUT_COL_BG3, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void*)(intptr_t)i);
        lv_obj_add_event_cb(row, _row_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, _id_at(i));
        lv_obj_set_style_text_font(name, PM_LAYOUT_FONT_TEXT, 0);
        lv_obj_set_style_text_color(name, PM_LAYOUT_COL_FG_BR, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, LV_PCT(100));
    }
}

static void _refresh_list(void) {
    s_count = pm_nosql_list("gemini_log", s_ids, MAX_SESSIONS, ID_SIZE);
    pm_log_i(TAG, "%d saved sessions", s_count);
    if (s_count_chip) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d SAVED", s_count);
        lv_label_set_text(s_count_chip, buf);
    }
    _populate_list();
}

static void _delete_cb(lv_event_t* e) {
    (void)e;
    if (s_open_id[0] == 0) return;
    pm_nosql_delete("gemini_log", s_open_id);
    s_open_id[0] = 0;
    if (s_viewer_lbl) {
        lv_label_set_text(s_viewer_lbl, "Select a session from the left.");
    }
    _refresh_list();
}

static void _refresh_cb(lv_event_t* e) { (void)e; _refresh_list(); }

// Public action shims (preserved for whatever called them)
void pm_app_gemini_log_action_up(void)     { /* legacy no-op */ }
void pm_app_gemini_log_action_down(void)   { /* legacy no-op */ }
void pm_app_gemini_log_action_open(void)   { /* legacy no-op */ }
void pm_app_gemini_log_action_back(void)   { /* legacy no-op */ }
void pm_app_gemini_log_action_delete(void) { /* legacy no-op */ }

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "GEMINI LOG");
    s_count_chip = pm_app_layout_chip(&L, "0 SAVED", PM_LAYOUT_COL_ACCENT);

    pm_app_layout_content(&L);

    // Left: session list
    lv_obj_t* left = pm_app_layout_pane(&L, 320, "SESSIONS");
    s_list_panel = lv_obj_create(left);
    lv_obj_remove_style_all(s_list_panel);
    lv_obj_set_width(s_list_panel, LV_PCT(100));
    lv_obj_set_flex_grow(s_list_panel, 1);
    lv_obj_set_style_bg_opa(s_list_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_list_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list_panel, LV_DIR_VER);

    // Right: viewer
    lv_obj_t* right = pm_app_layout_pane(&L, 0, "TRANSCRIPT");
    lv_obj_t* vbox = lv_obj_create(right);
    lv_obj_remove_style_all(vbox);
    lv_obj_set_width(vbox, LV_PCT(100));
    lv_obj_set_flex_grow(vbox, 1);
    lv_obj_set_style_bg_opa(vbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(vbox, 12, 0);
    lv_obj_add_flag(vbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(vbox, LV_DIR_VER);

    s_viewer_lbl = lv_label_create(vbox);
    lv_label_set_text(s_viewer_lbl, "Select a session from the left.");
    lv_obj_set_width(s_viewer_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_viewer_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_viewer_lbl, PM_LAYOUT_COL_FG, 0);
    lv_label_set_long_mode(s_viewer_lbl, LV_LABEL_LONG_WRAP);

    pm_app_layout_action(&L, "REFRESH", PM_LAYOUT_COL_ACCENT, _refresh_cb);
    pm_app_layout_action(&L, "DELETE",  PM_LAYOUT_COL_ERR,    _delete_cb);

    s_screen = pm_app_layout_end(&L);
}

static void _init(void) { pm_nosql_init("gemini_log"); }

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    _refresh_list();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_session_buf) { pm_psram_free(s_session_buf); s_session_buf = NULL; }
    s_screen = NULL;
}

static const pm_app_t _APP = {
    .id           = "gemini_log",
    .display_name = "GEMINI LOG",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_gemini_log(void) { return &_APP; }
