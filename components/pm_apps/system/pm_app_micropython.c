// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_micropython.c — REPL stub
//
//  Currently a placeholder. The launchable REPL UI is wired up
//  (input buffer, scrollback) so the integration layer only
//  has to fill in:
//    pm_upy_eval(const char* line, char* out_buf, size_t out_cap);
//
//  When MicroPython is statically linked, swap the stub eval
//  with mp_exec_str / mp_globals_get etc.
// ============================================================

#include "pm_app_micropython.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_UPY";

#define INPUT_MAX     256
#define SCROLLBACK    8192

static char  s_input[INPUT_MAX];
static int   s_input_len = 0;
static char* s_scrollback = NULL;     // PSRAM, 8KB ring of text
static int   s_scrollback_len = 0;

static void _scrollback_append(const char* s) {
    if (!s_scrollback) return;
    size_t add = strlen(s);
    if (s_scrollback_len + add + 1 >= SCROLLBACK) {
        // Drop oldest half on overflow
        int half = SCROLLBACK / 2;
        memmove(s_scrollback, s_scrollback + half, s_scrollback_len - half);
        s_scrollback_len -= half;
    }
    memcpy(s_scrollback + s_scrollback_len, s, add);
    s_scrollback_len += add;
    s_scrollback[s_scrollback_len] = 0;
    // TODO_LVGL: refresh scrollback widget.
}

// Stub evaluator — replace with mp_exec_str when MicroPython lands.
static void _eval(const char* line) {
    char out[128];
    snprintf(out, sizeof(out),
             ">>> %s\n[micropython not yet linked — line buffered]\n", line);
    _scrollback_append(out);
}

void pm_app_micropython_input_char(char c) {
    if (c == '\n' || c == '\r') {
        s_input[s_input_len] = 0;
        if (s_input_len > 0) _eval(s_input);
        s_input_len = 0;
    } else if (c == 8 /* BS */ || c == 127) {
        if (s_input_len > 0) s_input_len--;
    } else if (s_input_len < INPUT_MAX - 1) {
        s_input[s_input_len++] = c;
    }
    // TODO_LVGL: refresh input line widget.
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("uPY REPL",
        "uPY REPL app — UI ready");
}

static void _init(void) {
    s_scrollback = (char*)pm_psram_alloc(SCROLLBACK);
    if (s_scrollback) {
        s_scrollback[0] = 0;
        s_scrollback_len = 0;
        _scrollback_append("Pisces Moon P4 — uPython REPL (stub)\n");
        _scrollback_append("MicroPython not yet linked. Lines are echoed only.\n\n");
    }
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
}

static void _deinit(void) {
    if (s_scrollback) { pm_psram_free(s_scrollback); s_scrollback = NULL; }
}

static const pm_app_t _APP = {
    .id           = "micropython",
    .display_name = "uPY REPL",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_micropython(void) { return &_APP; }
