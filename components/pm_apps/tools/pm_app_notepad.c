// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_notepad.c — Plain-text note editor
//
//  Logic-port of S3 notepad.cpp. The S3 version used a
//  String buffer and Arduino_GFX direct draws. P4 uses a
//  PSRAM char buffer and an LVGL textarea (stubbed).
//
//  Save flow (header tap):
//    1. Buffer remains in PSRAM until exit.
//    2. "Save As" prompt overlay (filename input).
//    3. Write under PM_SPI_TAKE("notepad_save") to /sd/logs/.
//
//  Discipline: writes are wrapped in the SPI Treaty mutex
//  with the standard 500ms timeout. Keyboard input never
//  touches the SD card.
// ============================================================

#include "pm_app_notepad.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_NOTEPAD";

#define BUF_CAP        (64 * 1024)
#define LOG_DIR        "/sd/logs"
#define FILENAME_MAX_LEN 48

static char* s_buf       = NULL;     // PSRAM
static int   s_buf_len   = 0;
static char  s_filename[FILENAME_MAX_LEN] = "note";
static bool  s_dirty     = false;

// LVGL handles
static void* s_screen   = NULL;
static void* s_textarea = NULL;
static void* s_save_dlg = NULL;

// ─────────────────────────────────────────────
//  Buffer manipulation
// ─────────────────────────────────────────────
static void _ensure_buf(void) {
    if (!s_buf) {
        s_buf = (char*)pm_psram_alloc(BUF_CAP);
        if (s_buf) { s_buf[0] = 0; s_buf_len = 0; }
    }
}

static void _buf_append_char(char c) {
    if (!s_buf) return;
    if (s_buf_len >= BUF_CAP - 1) return;
    s_buf[s_buf_len++] = c;
    s_buf[s_buf_len]   = 0;
    s_dirty = true;
    // TODO_LVGL: lv_textarea_add_char(s_textarea, c);
}

static void _buf_backspace(void) {
    if (!s_buf || s_buf_len == 0) return;
    s_buf_len--;
    s_buf[s_buf_len] = 0;
    s_dirty = true;
    // TODO_LVGL: lv_textarea_del_char(s_textarea);
}

void pm_app_notepad_input_char(char c) {
    if (c == 8 || c == 127) {
        _buf_backspace();
    } else if (c == 10 || c == 13) {
        _buf_append_char('\n');
    } else if (c >= 32 && c < 127) {
        _buf_append_char(c);
    }
}

// ─────────────────────────────────────────────
//  Save
// ─────────────────────────────────────────────
static bool _save_to_sd(const char* filename) {
    if (!s_buf || !filename || !filename[0]) return false;

    char path[160];
    snprintf(path, sizeof(path), "%s/%s.txt", LOG_DIR, filename);

    bool ok = false;
    PM_SPI_TAKE("notepad_save") {
        // Ensure the logs directory exists.
        pm_file_mkdir(LOG_DIR);

        pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            size_t wrote = pm_file_write(f, s_buf, (size_t)s_buf_len);
            pm_file_close(f);
            ok = (wrote == (size_t)s_buf_len);
        }
    } PM_SPI_GIVE();

    if (ok) {
        pm_log_i(TAG, "saved %d bytes to %s", s_buf_len, path);
        s_dirty = false;
    } else {
        pm_log_e(TAG, "save failed: %s", path);
    }
    return ok;
}

void pm_app_notepad_save(const char* filename) {
    if (!filename) return;
    strncpy(s_filename, filename, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = 0;
    _save_to_sd(s_filename);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("JOURNAL",
        "JOURNAL app — UI ready");
}

static void _init(void) {
    _ensure_buf();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter (%d bytes in buffer)", s_buf_len);
    _ensure_buf();
    // TODO_LVGL: lv_textarea_set_text(s_textarea, s_buf);
}

static void _exit_(void) {
    pm_log_i(TAG, "exit (dirty=%d)", s_dirty);
    // The S3 design saves on exit via header tap. P4 will
    // present the save dialog from the UI; if the user hits
    // back without saving, the buffer is preserved in PSRAM
    // for next entry.
}

static void _deinit(void) {
    if (s_buf) { pm_psram_free(s_buf); s_buf = NULL; s_buf_len = 0; }
}

static const pm_app_t _APP = {
    .id           = "notepad",
    .display_name = "JOURNAL",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_notepad(void) { return &_APP; }
