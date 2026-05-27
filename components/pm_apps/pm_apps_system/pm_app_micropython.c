// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_micropython.c — REPL stub
//
//  REPL UI is wired (scrollback + input line + on-screen QWERTY).
//  Eval is stubbed pending MicroPython static-link integration.
// ============================================================

#include "pm_app_micropython.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_input.h"
#include "pm_app.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_UPY";

#define INPUT_MAX     256
#define SCROLLBACK    16384

static char  s_input[INPUT_MAX];
static int   s_input_len = 0;
static char* s_scrollback = NULL;     // PSRAM
static int   s_scrollback_len = 0;

// LVGL handles
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_scrollback_lbl = NULL;
static lv_obj_t* s_scrollback_box = NULL;
static lv_obj_t* s_input_lbl     = NULL;
static int       s_input_token   = -1;

static void _refresh_scrollback(void) {
    if (s_scrollback_lbl && s_scrollback)
        lv_label_set_text(s_scrollback_lbl, s_scrollback);
    if (s_scrollback_box)
        lv_obj_scroll_to_y(s_scrollback_box, LV_COORD_MAX, LV_ANIM_OFF);
}

static void _refresh_input(void) {
    if (!s_input_lbl) return;
    if (s_input_len == 0) {
        lv_label_set_text(s_input_lbl, ">>> ");
    } else {
        char buf[INPUT_MAX + 8];
        snprintf(buf, sizeof(buf), ">>> %s", s_input);
        lv_label_set_text(s_input_lbl, buf);
    }
}

static void _scrollback_append(const char* s) {
    if (!s_scrollback || !s) return;
    size_t add = strlen(s);
    if (s_scrollback_len + add + 1 >= SCROLLBACK) {
        int half = SCROLLBACK / 2;
        memmove(s_scrollback, s_scrollback + half, s_scrollback_len - half);
        s_scrollback_len -= half;
    }
    memcpy(s_scrollback + s_scrollback_len, s, add);
    s_scrollback_len += add;
    s_scrollback[s_scrollback_len] = 0;
    _refresh_scrollback();
}

static void _eval(const char* line) {
    char out[INPUT_MAX + 80];
    snprintf(out, sizeof(out),
             ">>> %s\n[micropython not yet linked — line buffered]\n", line);
    _scrollback_append(out);
}

void pm_app_micropython_input_char(char c) {
    if (c == '\n' || c == '\r') {
        s_input[s_input_len] = 0;
        if (s_input_len > 0) _eval(s_input);
        s_input_len = 0;
    } else if (c == 8 || c == 127) {
        if (s_input_len > 0) s_input_len--;
    } else if (s_input_len < INPUT_MAX - 1) {
        s_input[s_input_len++] = c;
    }
    _refresh_input();
}

static void _input_handler(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e || e->kind != PM_INPUT_KEY || !e->down) return;
    const pm_app_t* cur = pm_app_current();
    if (!cur || strcmp(cur->id, "micropython") != 0) return;
    char c = 0;
    if (e->code == PM_KEY_ENTER)          c = '\n';
    else if (e->code == PM_KEY_BACKSPACE) c = 8;
    else if (e->code >= 0x20 && e->code <= 0x7E) c = (char)e->code;
    if (c) pm_app_micropython_input_char(c);
}

static void _clear_cb(lv_event_t* e) {
    (void)e;
    if (!s_scrollback) return;
    s_scrollback[0] = 0;
    s_scrollback_len = 0;
    _scrollback_append("Cleared.\n");
}

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "MicroPython REPL");
    pm_app_layout_chip(&L, "STUB", PM_LAYOUT_COL_WARN);

    pm_app_layout_content(&L);

    lv_obj_t* pane = pm_app_layout_pane(&L, 0, "OUTPUT");
    lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);

    s_scrollback_box = lv_obj_create(pane);
    lv_obj_remove_style_all(s_scrollback_box);
    lv_obj_set_width(s_scrollback_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_scrollback_box, 1);
    lv_obj_set_style_bg_color(s_scrollback_box, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_scrollback_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scrollback_box, 10, 0);
    lv_obj_add_flag(s_scrollback_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_scrollback_box, LV_DIR_VER);

    s_scrollback_lbl = lv_label_create(s_scrollback_box);
    lv_label_set_text(s_scrollback_lbl, s_scrollback ? s_scrollback : "");
    lv_obj_set_width(s_scrollback_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_scrollback_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_scrollback_lbl, PM_LAYOUT_COL_FG, 0);
    lv_label_set_long_mode(s_scrollback_lbl, LV_LABEL_LONG_WRAP);

    // Input line at bottom of pane
    lv_obj_t* input_row = lv_obj_create(pane);
    lv_obj_remove_style_all(input_row);
    lv_obj_set_width(input_row, LV_PCT(100));
    lv_obj_set_height(input_row, 36);
    lv_obj_set_style_bg_color(input_row, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(input_row, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(input_row, 1, 0);
    lv_obj_set_style_border_side(input_row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_hor(input_row, 10, 0);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    s_input_lbl = lv_label_create(input_row);
    lv_label_set_text(s_input_lbl, ">>> ");
    lv_obj_set_width(s_input_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_input_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_input_lbl, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_align(s_input_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    pm_app_layout_action(&L, "CLEAR", PM_LAYOUT_COL_ERR, _clear_cb);

    s_screen = pm_app_layout_end(&L);
}

static void _init(void) {
    s_scrollback = (char*)pm_psram_alloc(SCROLLBACK);
    if (s_scrollback) {
        s_scrollback[0] = 0;
        s_scrollback_len = 0;
        _scrollback_append("Pisces Moon P4 — MicroPython REPL (stub)\n");
        _scrollback_append("MicroPython not yet linked. Lines are echoed only.\n\n");
    }
    _build_screen();
}

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    if (s_input_token < 0) {
        s_input_token = pm_input_subscribe(_input_handler, NULL);
    }
    _refresh_scrollback();
    _refresh_input();
    pm_log_i(TAG, "enter");
}

static void _exit_(void) {
    if (s_input_token >= 0) {
        pm_input_unsubscribe(s_input_token);
        s_input_token = -1;
    }
    pm_log_i(TAG, "exit");
}

static void _deinit(void) {
    if (s_scrollback) { pm_psram_free(s_scrollback); s_scrollback = NULL; }
    s_screen = NULL;
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
