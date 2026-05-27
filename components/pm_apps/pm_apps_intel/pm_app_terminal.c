// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_terminal.c — Gemini terminal (Phase-4 scaffold)
//
//  PSRAM-backed scrollback buffer, input line, history.
//  Send is gated on C6 having an HTTP capability — until
//  that's implemented in the C6 firmware, the app accepts
//  input and shows queued prompts but reports "no transport".
//
//  Once C6 supports {"cmd":"http_post"...} → {"event":"http_response"},
//  fill in _send_to_gemini() and the matching dispatch in main.c.
// ============================================================

#include "pm_app_terminal.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_TERM";

#define SCROLLBACK_SZ   (32 * 1024)     // 32 KB scrollback in PSRAM
#define INPUT_MAX        512
#define HISTORY_MAX      20

typedef struct {
    char role[8];   // "user" / "model"
    char text[512];
} msg_t;

// State
static char* s_scrollback = NULL;
static int   s_scrollback_len = 0;

static char  s_input[INPUT_MAX];
static int   s_input_len = 0;

static msg_t s_history[HISTORY_MAX];
static int   s_history_len = 0;

// LVGL
static void* s_screen = NULL;
static void* s_lbl_scrollback = NULL;
static void* s_lbl_input = NULL;
static void* s_lbl_status = NULL;

// ─────────────────────────────────────────────
//  Scrollback helpers
// ─────────────────────────────────────────────
static void _sb_print(const char* s) {
    if (!s_scrollback || !s) return;
    size_t add = strlen(s);
    if (add == 0) return;
    if (s_scrollback_len + add + 1 >= SCROLLBACK_SZ) {
        int half = SCROLLBACK_SZ / 2;
        memmove(s_scrollback, s_scrollback + half, s_scrollback_len - half);
        s_scrollback_len -= half;
    }
    memcpy(s_scrollback + s_scrollback_len, s, add);
    s_scrollback_len += add;
    s_scrollback[s_scrollback_len] = 0;
    // TODO_LVGL: lv_label_set_text(s_lbl_scrollback, s_scrollback);
}

static void _sb_printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _sb_print(buf);
}

// ─────────────────────────────────────────────
//  History management
// ─────────────────────────────────────────────
static void _history_push(const char* role, const char* text) {
    if (s_history_len >= HISTORY_MAX) {
        // Drop oldest pair
        memmove(&s_history[0], &s_history[2], sizeof(msg_t) * (HISTORY_MAX - 2));
        s_history_len -= 2;
    }
    msg_t* m = &s_history[s_history_len++];
    strncpy(m->role, role, sizeof(m->role) - 1); m->role[sizeof(m->role) - 1] = 0;
    strncpy(m->text, text, sizeof(m->text) - 1); m->text[sizeof(m->text) - 1] = 0;
}

void pm_app_terminal_reset(void) {
    s_history_len = 0;
    if (s_scrollback) { s_scrollback[0] = 0; s_scrollback_len = 0; }
    _sb_print("[memory cleared]\n");
}

// ─────────────────────────────────────────────
//  Send (stub — needs C6 HTTP transport)
// ─────────────────────────────────────────────
static void _send_to_gemini(const char* prompt) {
    _history_push("user", prompt);
    _sb_printf("> %s\n", prompt);

    // TODO when C6 has http_post:
    //   1. build JSON body from s_history[].
    //   2. pm_c6_cmd_send_raw with cmd=http_post, host=
    //      generativelanguage.googleapis.com, path=/v1beta/models/
    //      gemini-2.0-flash:generateContent?key=GEMINI_API_KEY,
    //      body=<built-json>.
    //   3. wait for {"event":"http_response","status":N,"body":"…"}
    //   4. parse → push assistant reply into s_history + scrollback.

    _sb_print("(gemini transport not yet online — prompt queued)\n");
    pm_log_w(TAG, "no transport — queued prompt: %.40s", prompt);

    // Persist session to NoSQL even without response, so the
    // user's prompts aren't lost.
    char path[80];
    snprintf(path, sizeof(path), "session_%u", (unsigned)pm_uptime_seconds());
    pm_nosql_append("gemini_log", path, prompt, strlen(prompt));
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void pm_app_terminal_input_char(char c) {
    if (c == '\n' || c == '\r') {
        s_input[s_input_len] = 0;
        if (s_input_len > 0) {
            _send_to_gemini(s_input);
        }
        s_input_len = 0;
    } else if (c == 8 || c == 127) {
        if (s_input_len > 0) s_input_len--;
    } else if (s_input_len < INPUT_MAX - 1) {
        s_input[s_input_len++] = c;
    }
    s_input[s_input_len] = 0;
    // TODO_LVGL: lv_label_set_text(s_lbl_input, s_input);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("TERMINAL",
        "TERMINAL app — UI ready");
}

static void _init(void) {
    s_scrollback = (char*)pm_psram_alloc(SCROLLBACK_SZ);
    if (s_scrollback) { s_scrollback[0] = 0; s_scrollback_len = 0; }
    pm_nosql_init("gemini_log");
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _sb_print("Pisces Moon — Gemini Terminal\n");
    _sb_print("Type prompt and press ENTER. Q to clear memory.\n\n");
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_scrollback) { pm_psram_free(s_scrollback); s_scrollback = NULL; }
}

static const pm_app_t _APP = {
    .id           = "terminal",
    .display_name = "TERMINAL",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_terminal(void) { return &_APP; }
