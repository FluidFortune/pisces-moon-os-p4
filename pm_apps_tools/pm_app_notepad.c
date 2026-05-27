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
#include "pm_input.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
static lv_obj_t* s_screen   = NULL;
static void* s_textarea = NULL;
static void* s_save_dlg = NULL;
static lv_obj_t*         s_ta_obj = NULL;
static pm_ui_keyboard_t* s_kb     = NULL;

#define INPUT_QUEUE_CAP 32
static SemaphoreHandle_t s_input_mutex = NULL;
static pm_input_event_t  s_input_q[INPUT_QUEUE_CAP];
static uint8_t           s_input_head = 0;
static uint8_t           s_input_tail = 0;
static int               s_input_token = -1;

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

static bool _ensure_input_mutex(void) {
    if (s_input_mutex) return true;
    s_input_mutex = xSemaphoreCreateMutex();
    if (!s_input_mutex) {
        pm_log_w(TAG, "input queue mutex create failed");
        return false;
    }
    return true;
}

static void _on_input(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e || e->kind != PM_INPUT_KEY || !e->down) return;
    if (!_ensure_input_mutex()) return;
    if (xSemaphoreTake(s_input_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    uint8_t next = (uint8_t)((s_input_head + 1) % INPUT_QUEUE_CAP);
    if (next != s_input_tail) {
        s_input_q[s_input_head] = *e;
        s_input_head = next;
    }

    xSemaphoreGive(s_input_mutex);
}

static void _apply_key(const pm_input_event_t* e) {
    if (!e || !s_ta_obj) return;

    switch (e->code) {
    case PM_KEY_BACKSPACE:
    case PM_KEY_DELETE:
        lv_textarea_delete_char(s_ta_obj);
        s_dirty = true;
        break;
    case PM_KEY_ENTER:
        lv_textarea_add_char(s_ta_obj, '\n');
        s_dirty = true;
        break;
    case PM_KEY_TAB:
        lv_textarea_add_text(s_ta_obj, "    ");
        s_dirty = true;
        break;
    case PM_KEY_LEFT:
        lv_textarea_cursor_left(s_ta_obj);
        break;
    case PM_KEY_RIGHT:
        lv_textarea_cursor_right(s_ta_obj);
        break;
    case PM_KEY_UP:
        lv_textarea_cursor_up(s_ta_obj);
        break;
    case PM_KEY_DOWN:
        lv_textarea_cursor_down(s_ta_obj);
        break;
    case PM_KEY_HOME:
        lv_textarea_set_cursor_pos(s_ta_obj, 0);
        break;
    case PM_KEY_END:
        lv_textarea_set_cursor_pos(s_ta_obj, LV_TEXTAREA_CURSOR_LAST);
        break;
    default:
        if (e->code >= 32 && e->code <= 126) {
            lv_textarea_add_char(s_ta_obj, (uint32_t)e->code);
            s_dirty = true;
        }
        break;
    }
}

static void _flush_input(void) {
    pm_input_event_t local[INPUT_QUEUE_CAP];
    uint8_t n = 0;

    if (!_ensure_input_mutex()) return;
    if (xSemaphoreTake(s_input_mutex, 0) != pdTRUE) return;
    while (s_input_tail != s_input_head && n < INPUT_QUEUE_CAP) {
        local[n++] = s_input_q[s_input_tail];
        s_input_tail = (uint8_t)((s_input_tail + 1) % INPUT_QUEUE_CAP);
    }
    xSemaphoreGive(s_input_mutex);

    if (n == 0) return;
    if (!lvgl_port_lock(0)) return;
    for (uint8_t i = 0; i < n; i++) {
        _apply_key(&local[i]);
    }
    lvgl_port_unlock();
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
//  Lifecycle — Phase 14: real LVGL textarea + on-screen keyboard
// ─────────────────────────────────────────────

static void _save_clicked(lv_event_t* e) {
    (void)e;
    // For now, save under the default filename. A filename
    // prompt overlay will come in a later phase.
    const char* txt = lv_textarea_get_text(s_ta_obj);
    if (txt) {
        // Sync to backing buffer before SD write
        size_t n = strlen(txt);
        if (n >= BUF_CAP) n = BUF_CAP - 1;
        memcpy(s_buf, txt, n);
        s_buf[n] = 0;
        s_buf_len = (int)n;
        s_dirty = true;
    }
    _save_to_sd(s_filename);
    s_dirty = false;
}

static void _ta_focused(lv_event_t* e) {
    (void)e;
    pm_ui_keyboard_show(s_kb);
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "JOURNAL", NULL, NULL);

    // Save button row
    lv_obj_t* row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    pm_ui_button(row, "SAVE", _save_clicked, NULL);

    // Text area
    s_ta_obj = lv_textarea_create(s_screen);
    lv_obj_set_width(s_ta_obj, LV_PCT(100));
    lv_obj_set_flex_grow(s_ta_obj, 1);
    lv_obj_set_style_bg_color   (s_ta_obj, PM_C_BG_2,    0);
    lv_obj_set_style_text_color (s_ta_obj, PM_C_FG,      0);
    lv_obj_set_style_border_color(s_ta_obj, PM_C_BORDER, 0);
    lv_textarea_set_placeholder_text(s_ta_obj, "Type a note...");
    lv_obj_add_event_cb(s_ta_obj, _ta_focused, LV_EVENT_FOCUSED, NULL);

    // On-screen keyboard (hidden until textarea gets focus)
    s_kb = pm_ui_keyboard_create(s_screen);
    pm_ui_keyboard_attach(s_kb, s_ta_obj);
}

static void _init(void) {
    _ensure_buf();
    _build_screen();
}

static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter (%d bytes in buffer)", s_buf_len);
    _ensure_buf();
    if (s_input_token < 0) {
        s_input_token = pm_input_subscribe(_on_input, NULL);
    }
    // Restore last buffer text into the textarea
    if (s_ta_obj && s_buf && s_buf[0]) {
        lv_textarea_set_text(s_ta_obj, s_buf);
    }
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    _flush_input();
}

static void _exit_(void) {
    if (s_input_token >= 0) {
        pm_input_unsubscribe(s_input_token);
        s_input_token = -1;
    }
    // Capture textarea state into the backing buffer for
    // next entry. (Save-on-exit is opt-in via the Save button.)
    if (s_ta_obj) {
        const char* txt = lv_textarea_get_text(s_ta_obj);
        if (txt) {
            size_t n = strlen(txt);
            if (n >= BUF_CAP) n = BUF_CAP - 1;
            memcpy(s_buf, txt, n);
            s_buf[n] = 0;
            s_buf_len = (int)n;
        }
    }
    pm_log_i(TAG, "exit (dirty=%d)", s_dirty);
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
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_notepad(void) { return &_APP; }
