// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_files.c — SD file browser
//
//  Two views:
//    LIST    — directory listing of current path. Folders
//              first, then files. Cursor + scroll.
//    PREVIEW — file contents loaded into PSRAM. Paginated.
//
//  SPI Treaty discipline:
//    - Listing the directory:  one PM_SPI_TAKE / GIVE block.
//    - Loading file content:   one PM_SPI_TAKE / GIVE block,
//      releases the mutex once the bytes are in PSRAM.
//    - Pagination through the PSRAM buffer needs no mutex.
//
//  Limits:
//    MAX_ENTRIES   — 256 entries per directory
//    MAX_PREVIEW   — 256 KB preview cap
// ============================================================

#include "pm_app_files.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "PM_FILES";

#define MAX_ENTRIES   256
#define MAX_PATH      256
#define MAX_PREVIEW   (256 * 1024)
#define ENTRIES_PER_PAGE 16

typedef struct {
    char  name[96];
    bool  is_dir;
} fs_entry_t;

typedef enum {
    VIEW_LIST,
    VIEW_PREVIEW,
} files_view_t;

// State
static files_view_t s_view       = VIEW_LIST;
static char         s_path[MAX_PATH] = "/sd";
static fs_entry_t*  s_entries    = NULL;     // dynamically alloced (PSRAM)
static int          s_entry_cnt  = 0;
static int          s_cursor     = 0;
static int          s_scroll     = 0;

static char*        s_preview    = NULL;     // PSRAM buffer
static size_t       s_preview_sz = 0;
static int          s_preview_offset = 0;    // byte offset for paging

static void* s_screen      = NULL;
static void* s_lbl_path    = NULL;
static void* s_list_widget = NULL;
static void* s_preview_widget = NULL;

// ─────────────────────────────────────────────
//  Comparator for qsort: dirs first, then alpha
// ─────────────────────────────────────────────
static int _entry_cmp(const void* a, const void* b) {
    const fs_entry_t* ea = (const fs_entry_t*)a;
    const fs_entry_t* eb = (const fs_entry_t*)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

// ─────────────────────────────────────────────
//  Load directory under SPI mutex
// ─────────────────────────────────────────────
static bool _load_directory(const char* path) {
    if (!s_entries) {
        s_entries = (fs_entry_t*)pm_psram_alloc(sizeof(fs_entry_t) * MAX_ENTRIES);
        if (!s_entries) {
            pm_log_e(TAG, "PSRAM alloc for entries failed");
            return false;
        }
    }
    s_entry_cnt = 0;

    bool ok = false;
    PM_SPI_TAKE("files_listdir") {
        pm_dir_t* d = pm_dir_open(path);
        if (d) {
            const char* name;
            bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL &&
                   s_entry_cnt < MAX_ENTRIES) {
                if (strcmp(name, ".") == 0) continue;
                strncpy(s_entries[s_entry_cnt].name, name,
                        sizeof(s_entries[s_entry_cnt].name) - 1);
                s_entries[s_entry_cnt].name[sizeof(s_entries[s_entry_cnt].name)-1] = 0;
                s_entries[s_entry_cnt].is_dir = is_dir;
                s_entry_cnt++;
            }
            pm_dir_close(d);
            ok = true;
        }
    } PM_SPI_GIVE();

    if (!ok) {
        pm_log_w(TAG, "could not open '%s'", path);
        return false;
    }

    qsort(s_entries, s_entry_cnt, sizeof(fs_entry_t), _entry_cmp);
    s_cursor = 0;
    s_scroll = 0;
    return true;
}

// ─────────────────────────────────────────────
//  Load file content into PSRAM under SPI mutex
// ─────────────────────────────────────────────
static bool _load_preview(const char* path) {
    if (s_preview) { pm_psram_free(s_preview); s_preview = NULL; }
    s_preview_sz = 0;
    s_preview_offset = 0;

    bool ok = false;
    PM_SPI_TAKE("files_preview") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            size_t sz = pm_file_size(f);
            if (sz > MAX_PREVIEW) sz = MAX_PREVIEW;
            s_preview = (char*)pm_psram_alloc(sz + 1);
            if (s_preview) {
                size_t got = pm_file_read(f, s_preview, sz);
                s_preview[got] = 0;
                s_preview_sz   = got;
                ok = true;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    if (!ok) pm_log_w(TAG, "preview load failed: %s", path);
    return ok;
}

// ─────────────────────────────────────────────
//  Path navigation
// ─────────────────────────────────────────────
static void _path_join(char* dst, size_t cap,
                        const char* base, const char* leaf) {
    size_t bl = strlen(base);
    bool need_slash = (bl > 0 && base[bl - 1] != '/');
    snprintf(dst, cap, "%s%s%s", base, need_slash ? "/" : "", leaf);
}

static void _path_pop(char* path) {
    char* slash = strrchr(path, '/');
    if (!slash) return;
    if (slash == path) { path[1] = 0; return; }    // root
    *slash = 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_list(void) {
    // TODO_LVGL: rebuild the list widget rows from s_entries.
    // Show s_entries[s_scroll .. s_scroll + ENTRIES_PER_PAGE - 1].
    // Highlight s_cursor row.
    // Path label = s_path.
    pm_log_d(TAG, "list view: %d entries in '%s'", s_entry_cnt, s_path);
}

static void _render_preview(void) {
    // TODO_LVGL: show ~30 lines starting at s_preview_offset.
    pm_log_d(TAG, "preview view: %zu bytes loaded", s_preview_sz);
}

// ─────────────────────────────────────────────
//  Input dispatch (called from LVGL events / key handler)
// ─────────────────────────────────────────────
void pm_app_files_action_up(void) {
    if (s_view == VIEW_LIST) {
        if (s_cursor > 0) s_cursor--;
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        _render_list();
    }
}

void pm_app_files_action_down(void) {
    if (s_view == VIEW_LIST) {
        if (s_cursor < s_entry_cnt - 1) s_cursor++;
        if (s_cursor >= s_scroll + ENTRIES_PER_PAGE)
            s_scroll = s_cursor - ENTRIES_PER_PAGE + 1;
        _render_list();
    }
}

void pm_app_files_action_open(void) {
    if (s_view != VIEW_LIST) return;
    if (s_entry_cnt == 0) return;
    const fs_entry_t* e = &s_entries[s_cursor];
    if (strcmp(e->name, "..") == 0) {
        _path_pop(s_path);
        _load_directory(s_path);
        _render_list();
        return;
    }
    char joined[MAX_PATH];
    _path_join(joined, sizeof(joined), s_path, e->name);
    if (e->is_dir) {
        strncpy(s_path, joined, sizeof(s_path) - 1);
        s_path[sizeof(s_path) - 1] = 0;
        _load_directory(s_path);
        _render_list();
    } else {
        if (_load_preview(joined)) {
            s_view = VIEW_PREVIEW;
            _render_preview();
        }
    }
}

void pm_app_files_action_back(void) {
    if (s_view == VIEW_PREVIEW) {
        if (s_preview) { pm_psram_free(s_preview); s_preview = NULL; }
        s_preview_sz = 0;
        s_view = VIEW_LIST;
        _render_list();
    }
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("FILES",
        "FILES app — UI ready");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    if (!pm_sd_mounted()) {
        pm_log_w(TAG, "SD not mounted — attempting");
        pm_sd_mount();
    }
    strncpy(s_path, "/sd", sizeof(s_path) - 1);
    _load_directory(s_path);
    s_view = VIEW_LIST;
    _render_list();
    // TODO_LVGL: lv_scr_load(s_screen);
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (s_preview) { pm_psram_free(s_preview); s_preview = NULL; s_preview_sz = 0; }
    // Keep s_entries allocated — small, reused on next enter.
}

static void _deinit(void) {
    if (s_entries) { pm_psram_free(s_entries); s_entries = NULL; }
    if (s_preview) { pm_psram_free(s_preview); s_preview = NULL; }
}

static const pm_app_t _APP = {
    .id           = "files",
    .display_name = "FILES",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_files(void) { return &_APP; }
