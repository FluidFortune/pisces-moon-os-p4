// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_ereader.c — SD card text/markdown reader
//
//  Library directory:  /sd/books/   (.txt and .md files)
//                      falls back to /sd/ if /sd/books missing
//
//  Reading model:
//    - File body is loaded into PSRAM (up to MAX_BOOK_BYTES).
//    - An LV_LABEL with LV_LABEL_LONG_WRAP displays the text.
//    - The container scrolls vertically so the user can swipe
//      / drag through the book. No fixed pagination — the panel
//      flows like a long page.
//    - Bookmarks persist the scroll Y per file via pm_nosql under
//      the "ereader_bm" namespace.
//
//  Why no custom wrap engine?
//    LVGL already handles word wrapping and Unicode line breaks
//    for us inside an LV_LABEL. Trying to reimplement that on top
//    of a chunked byte cursor (the S3 approach for tiny RAM
//    builds) wastes effort on a device with 32 MB PSRAM and a
//    1024×600 display. We use the platform's strength.
// ============================================================

#include "pm_app_ereader.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_EREADER";

#define MAX_BOOKS         128
#define MAX_BOOK_BYTES    (1024 * 1024)   // 1 MB cap per book
#define NAME_LEN          80
#define NOSQL_CAT         "ereader_bm"

typedef struct {
    char name[NAME_LEN];
    char path[160];
    uint32_t size_bytes;
} book_t;

static book_t* s_books = NULL;     // PSRAM
static int     s_book_count = 0;
static int     s_selected = -1;
static char*   s_body = NULL;      // PSRAM
static size_t  s_body_sz = 0;
static char    s_open_path[160] = "";

// UI handles
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_read_pane     = NULL;
static lv_obj_t* s_read_scroll   = NULL;
static lv_obj_t* s_read_label    = NULL;
static lv_obj_t* s_title_lbl     = NULL;
static lv_obj_t* s_meta_lbl      = NULL;
static lv_obj_t* s_chip_count    = NULL;
static lv_obj_t* s_stat_books    = NULL;
static lv_obj_t* s_stat_size     = NULL;
static lv_obj_t* s_stat_position = NULL;
static bool      s_built         = false;

// Per-book UI rows (pre-allocated for snappy redraws).
typedef struct {
    lv_obj_t* row;
    lv_obj_t* name_lbl;
    lv_obj_t* size_lbl;
} book_row_ui_t;
#define MAX_VISIBLE_BOOKS 32
static book_row_ui_t s_rows[MAX_VISIBLE_BOOKS];
static int           s_rows_created = 0;

// ── Helpers ──────────────────────────────────────────────
static const char* _ext_of(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot ? dot : "";
}
static bool _is_book(const char* name) {
    const char* e = _ext_of(name);
    return (strcasecmp(e, ".txt") == 0 ||
            strcasecmp(e, ".md")  == 0 ||
            strcasecmp(e, ".markdown") == 0);
}

static int _book_cmp(const void* a, const void* b) {
    return strcasecmp(((const book_t*)a)->name, ((const book_t*)b)->name);
}

static bool _scan_dir(const char* path) {
    bool any = false;
    PM_SPI_TAKE("ereader_scan") {
        pm_dir_t* d = pm_dir_open(path);
        if (d) {
            const char* name;
            bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL &&
                   s_book_count < MAX_BOOKS) {
                if (is_dir) continue;
                if (!_is_book(name)) continue;
                book_t* e = &s_books[s_book_count++];
                strncpy(e->name, name, sizeof(e->name) - 1);
                e->name[sizeof(e->name) - 1] = 0;
                snprintf(e->path, sizeof(e->path), "%s/%s", path, name);
                pm_file_t* f = pm_file_open(e->path, PM_FILE_READ);
                if (f) {
                    e->size_bytes = (uint32_t)pm_file_size(f);
                    pm_file_close(f);
                }
                any = true;
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
    return any;
}

static void _rescan(void) {
    s_book_count = 0;
    if (!s_books) {
        s_books = (book_t*)pm_psram_alloc(sizeof(book_t) * MAX_BOOKS);
        if (!s_books) {
            pm_log_e(TAG, "PSRAM alloc for books failed");
            return;
        }
    }
    memset(s_books, 0, sizeof(book_t) * MAX_BOOKS);

    // Prefer /sd/books, fall back to /sd root.
    bool found = _scan_dir("/sd/books");
    if (!found) {
        pm_log_i(TAG, "/sd/books empty — scanning /sd root");
        _scan_dir("/sd");
    }
    if (s_book_count > 1) {
        qsort(s_books, s_book_count, sizeof(book_t), _book_cmp);
    }
}

// ── Load a book ──────────────────────────────────────────
static bool _load_book(const char* path) {
    if (s_body) { pm_psram_free(s_body); s_body = NULL; s_body_sz = 0; }
    bool ok = false;
    PM_SPI_TAKE("ereader_load") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            size_t sz = pm_file_size(f);
            if (sz > MAX_BOOK_BYTES) sz = MAX_BOOK_BYTES;
            s_body = (char*)pm_psram_alloc(sz + 1);
            if (s_body) {
                size_t got = pm_file_read(f, s_body, sz);
                s_body[got] = '\0';
                s_body_sz   = got;
                ok = true;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    if (!ok) pm_log_w(TAG, "load failed: %s", path);
    return ok;
}

// ── Bookmark persistence (scroll Y as decimal text) ──────
static int _bm_load(const char* path) {
    char buf[16] = {0};
    size_t got = pm_nosql_read(NOSQL_CAT, path, buf, sizeof(buf) - 1);
    if (got == 0) return 0;
    return atoi(buf);
}
static void _bm_save(const char* path, int scroll_y) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", scroll_y);
    pm_nosql_write(NOSQL_CAT, path, buf, strlen(buf));
}

// ── Open a book in the reader pane ───────────────────────
static void _open_book(int idx) {
    if (idx < 0 || idx >= s_book_count) return;
    if (s_open_path[0] && s_read_scroll) {
        // Stash current scroll for the previously-open book.
        int y = lv_obj_get_scroll_y(s_read_scroll);
        _bm_save(s_open_path, y);
    }
    s_selected = idx;
    const book_t* b = &s_books[idx];
    strncpy(s_open_path, b->path, sizeof(s_open_path) - 1);
    s_open_path[sizeof(s_open_path) - 1] = 0;

    if (!_load_book(b->path)) return;

    if (s_read_label) lv_label_set_text(s_read_label, s_body);
    if (s_title_lbl)  lv_label_set_text(s_title_lbl, b->name);
    if (s_meta_lbl) {
        char m[48];
        snprintf(m, sizeof(m), "%lu bytes", (unsigned long)b->size_bytes);
        lv_label_set_text(s_meta_lbl, m);
    }

    int saved_y = _bm_load(b->path);
    if (s_read_scroll && saved_y > 0) {
        lv_obj_scroll_to_y(s_read_scroll, saved_y, LV_ANIM_OFF);
    } else if (s_read_scroll) {
        lv_obj_scroll_to_y(s_read_scroll, 0, LV_ANIM_OFF);
    }
}

// ── List rows ────────────────────────────────────────────
static void _on_row_clicked(lv_event_t* e) {
    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    _open_book(idx);
}

static void _build_row(int idx, lv_obj_t* parent) {
    book_row_ui_t* r = &s_rows[idx];
    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 36);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->row, _on_row_clicked, LV_EVENT_CLICKED, NULL);

    r->name_lbl = lv_label_create(r->row);
    lv_label_set_text(r->name_lbl, "—");
    lv_label_set_long_mode(r->name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(r->name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->name_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_flex_grow(r->name_lbl, 1);

    r->size_lbl = lv_label_create(r->row);
    lv_label_set_text(r->size_lbl, "—");
    lv_obj_set_style_text_font(r->size_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->size_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->size_lbl, 64);
    lv_obj_set_style_text_align(r->size_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

static void _render_list(void) {
    int total_bytes = 0;
    int visible = 0;
    for (int i = 0; i < s_book_count && i < MAX_VISIBLE_BOOKS; i++) {
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        book_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(r->row, (void*)(intptr_t)i);
        lv_label_set_text(r->name_lbl, s_books[i].name);
        char sb[16];
        if (s_books[i].size_bytes < 1024)
            snprintf(sb, sizeof(sb), "%lu B", (unsigned long)s_books[i].size_bytes);
        else if (s_books[i].size_bytes < 1024 * 1024)
            snprintf(sb, sizeof(sb), "%.1fK", s_books[i].size_bytes / 1024.0);
        else
            snprintf(sb, sizeof(sb), "%.1fM", s_books[i].size_bytes / (1024.0 * 1024.0));
        lv_label_set_text(r->size_lbl, sb);
        total_bytes += s_books[i].size_bytes;
        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_chip_count) {
        char b[24]; snprintf(b, sizeof(b), "%d BOOKS", s_book_count);
        lv_label_set_text(s_chip_count, b);
    }
    if (s_stat_books) {
        char b[12]; snprintf(b, sizeof(b), "%d", s_book_count);
        lv_label_set_text(s_stat_books, b);
    }
    if (s_stat_size) {
        char b[24];
        if (total_bytes < 1024)              snprintf(b, sizeof(b), "%d B",   total_bytes);
        else if (total_bytes < 1024 * 1024)  snprintf(b, sizeof(b), "%.1fK",  total_bytes / 1024.0);
        else                                 snprintf(b, sizeof(b), "%.1fM",  total_bytes / (1024.0 * 1024.0));
        lv_label_set_text(s_stat_size, b);
    }
}

// ── Actions ──────────────────────────────────────────────
static void _act_rescan(lv_event_t* e) { (void)e; _rescan(); _render_list(); }
static void _act_bookmark(lv_event_t* e) {
    (void)e;
    if (!s_open_path[0] || !s_read_scroll) return;
    int y = lv_obj_get_scroll_y(s_read_scroll);
    _bm_save(s_open_path, y);
    pm_log_i(TAG, "bookmark saved @ y=%d", y);
}
static void _act_top(lv_event_t* e) {
    (void)e;
    if (s_read_scroll) lv_obj_scroll_to_y(s_read_scroll, 0, LV_ANIM_ON);
}

// ── Build ────────────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "EREADER");

    s_chip_count = pm_app_layout_chip(&L, "0 BOOKS", PM_LAYOUT_COL_ACCENT);

    pm_app_layout_stats_row(&L, 3);
    s_stat_books    = pm_app_layout_stat(&L, "BOOKS",    "0");
    s_stat_size     = pm_app_layout_stat(&L, "LIBRARY",  "0 B");
    s_stat_position = pm_app_layout_stat(&L, "POSITION", "—");

    pm_app_layout_content(&L);

    // Left: library list
#if PM_BOARD_LCD_H_RES <= 800
    int lib_w = 260;
#else
    int lib_w = 360;
#endif
    lv_obj_t* lib_pane = pm_app_layout_pane(&L, lib_w, "LIBRARY");
    s_list_box = lv_obj_create(lib_pane);
    lv_obj_remove_style_all(s_list_box);
    lv_obj_set_width(s_list_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_list_box, 1);
    lv_obj_set_style_bg_opa(s_list_box, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_list_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list_box, 1, 0);
    lv_obj_add_flag(s_list_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_box, LV_SCROLLBAR_MODE_AUTO);

    // Right: reading pane
    s_read_pane = pm_app_layout_pane(&L, 0, "READING");

    // Title strip
    lv_obj_t* title_strip = lv_obj_create(s_read_pane);
    lv_obj_remove_style_all(title_strip);
    lv_obj_set_width(title_strip, LV_PCT(100));
    lv_obj_set_height(title_strip, 28);
    lv_obj_set_style_bg_color(title_strip, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(title_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title_strip, 12, 0);
    lv_obj_set_layout(title_strip, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_strip, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_strip, LV_OBJ_FLAG_SCROLLABLE);
    s_title_lbl = lv_label_create(title_strip);
    lv_label_set_text(s_title_lbl, "(select a book on the left)");
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_title_lbl, 1);
    lv_obj_set_style_text_font(s_title_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_title_lbl, PM_LAYOUT_COL_FG_BR, 0);
    s_meta_lbl = lv_label_create(title_strip);
    lv_label_set_text(s_meta_lbl, "—");
    lv_obj_set_style_text_font(s_meta_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_meta_lbl, PM_LAYOUT_COL_DIM, 0);

    // Scrollable text body
    s_read_scroll = lv_obj_create(s_read_pane);
    lv_obj_remove_style_all(s_read_scroll);
    lv_obj_set_width(s_read_scroll, LV_PCT(100));
    lv_obj_set_flex_grow(s_read_scroll, 1);
    lv_obj_set_style_bg_color(s_read_scroll, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_read_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_read_scroll, 14, 0);
    lv_obj_add_flag(s_read_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_read_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_read_scroll, LV_SCROLLBAR_MODE_AUTO);

    s_read_label = lv_label_create(s_read_scroll);
    lv_label_set_text(s_read_label, "Tap a book in the LIBRARY pane to start reading.");
    lv_label_set_long_mode(s_read_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_read_label, LV_PCT(100));
    lv_obj_set_style_text_font(s_read_label, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_read_label, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_style_text_line_space(s_read_label, 4, 0);

    pm_app_layout_action(&L, "RESCAN",   PM_LAYOUT_COL_ACCENT, _act_rescan);
    pm_app_layout_action(&L, "BOOKMARK", PM_LAYOUT_COL_GOLD,   _act_bookmark);
    pm_app_layout_action(&L, "TOP",      PM_LAYOUT_COL_PURPLE, _act_top);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ────────────────────────────────────────────
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    if (!pm_sd_mounted()) pm_sd_mount();
    if (s_book_count == 0) _rescan();
    _render_list();
}
static void _exit_(void) {
    // Stash bookmark for the currently-open book.
    if (s_open_path[0] && s_read_scroll) {
        int y = lv_obj_get_scroll_y(s_read_scroll);
        _bm_save(s_open_path, y);
    }
}
static void _deinit(void) {
    if (s_body)  { pm_psram_free(s_body);  s_body  = NULL; }
    if (s_books) { pm_psram_free(s_books); s_books = NULL; }
}

static const pm_app_t _APP = {
    .id           = "ereader",
    .display_name = "EREADER",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_ereader(void) { return &_APP; }
