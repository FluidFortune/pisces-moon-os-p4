// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gemini_log.c — Saved chat browser
// ============================================================

#include "pm_app_gemini_log.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_GEMLOG";

#define MAX_SESSIONS 64
#define ID_SIZE      40

typedef enum { VIEW_LIST, VIEW_SESSION } view_t;

static view_t s_view = VIEW_LIST;
static char   s_ids[MAX_SESSIONS * ID_SIZE];
static int    s_count = 0;
static int    s_cursor = 0;
static char   s_open_id[ID_SIZE] = "";

static char*  s_session_buf = NULL;     // PSRAM
#define SESSION_BUF_SZ (128 * 1024)

static void _refresh_list(void) {
    s_count = pm_nosql_list("gemini_log",
                            s_ids, MAX_SESSIONS, ID_SIZE);
    pm_log_i(TAG, "%d saved sessions", s_count);
    // TODO_LVGL: rebuild list rows
}

static const char* _id_at(int i) {
    if (i < 0 || i >= s_count) return "";
    return &s_ids[i * ID_SIZE];
}

static void _open_session(int idx) {
    if (idx < 0 || idx >= s_count) return;
    if (!s_session_buf) {
        s_session_buf = (char*)pm_psram_alloc(SESSION_BUF_SZ);
        if (!s_session_buf) return;
    }

    const char* id = _id_at(idx);
    strncpy(s_open_id, id, sizeof(s_open_id) - 1);
    s_open_id[sizeof(s_open_id) - 1] = 0;

    size_t got = pm_nosql_read("gemini_log", id,
                                s_session_buf, SESSION_BUF_SZ);
    pm_log_i(TAG, "loaded session '%s' (%zu bytes)", id, got);
    s_view = VIEW_SESSION;
    // TODO_LVGL: lv_label_set_text(viewer, s_session_buf);
}

static void _back(void) {
    s_view = VIEW_LIST;
    // TODO_LVGL: rebuild list view
}

static void _delete_open(void) {
    if (s_open_id[0] == 0) return;
    pm_nosql_delete("gemini_log", s_open_id);
    s_open_id[0] = 0;
    _refresh_list();
    _back();
}

void pm_app_gemini_log_action_up(void) {
    if (s_view == VIEW_LIST && s_cursor > 0) s_cursor--;
}
void pm_app_gemini_log_action_down(void) {
    if (s_view == VIEW_LIST && s_cursor < s_count - 1) s_cursor++;
}
void pm_app_gemini_log_action_open(void) {
    if (s_view == VIEW_LIST) _open_session(s_cursor);
}
void pm_app_gemini_log_action_back(void)   { _back(); }
void pm_app_gemini_log_action_delete(void) { _delete_open(); }

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("GEMINI LOG",
        "GEMINI LOG app — UI ready");
}

static void _init(void) {
    pm_nosql_init("gemini_log");
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _refresh_list();
    s_view = VIEW_LIST;
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_session_buf) { pm_psram_free(s_session_buf); s_session_buf = NULL; }
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
