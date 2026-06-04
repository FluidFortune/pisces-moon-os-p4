// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_wardrive_inspect.c — "Ghost Ride The Whip"
//
//  Read-only diagnostic browser for Wardrive's outputs.
//
//  Scans:
//    /sd/exports/wardrive_*.csv   — CSV exports from wardrive
//    /sd/exports/wardrive_*.json  — JSON exports
//    /sd/sessions/*.db            — live SQLite session files
//
//  Per-file info:
//    - name
//    - size in bytes
//    - line count (approximate, '\n' counted in 4 KB chunks)
//    - health verdict:
//        GREEN  size > 256 bytes and lines > 1   → working
//        AMBER  header-only (1 line, small)      → writes failed
//        RED    empty                            → file never written
//
//  Wardrive can keep running while we look. Every SD touch is
//  gated by PM_SPI_TAKE so we don't fight the Ghost Engine's
//  active CSV writes.
// ============================================================

#include "pm_app_wardrive_inspect.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_WD_INSPECT";

#define MAX_FILES        64
#define LINE_CHUNK       4096   // bytes per read when counting lines
#define MAX_VISIBLE_ROWS 24

typedef enum {
    WI_SRC_CSV_EXPORTS  = 0,
    WI_SRC_JSON_EXPORTS,
    WI_SRC_SESSIONS_DB,
    WI_SRC_COUNT
} wi_src_t;

typedef struct {
    char     name[80];
    char     path[160];
    uint64_t size_bytes;
    uint32_t line_count;
    wi_src_t source;
} wi_file_t;

static wi_file_t* s_files = NULL;     // PSRAM
static int        s_count = 0;
static bool       s_scan_busy = false;

// ── UI state ─────────────────────────────────────────────
typedef struct {
    lv_obj_t* row;
    lv_obj_t* status_dot;
    lv_obj_t* name_lbl;
    lv_obj_t* size_lbl;
    lv_obj_t* rows_lbl;
    lv_obj_t* badge_lbl;
} wi_row_ui_t;

static wi_row_ui_t s_rows[MAX_VISIBLE_ROWS];
static int         s_rows_created = 0;

static lv_obj_t* s_chip_status   = NULL;
static lv_obj_t* s_stat_files    = NULL;
static lv_obj_t* s_stat_bytes    = NULL;
static lv_obj_t* s_stat_rows     = NULL;
static lv_obj_t* s_stat_healthy  = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_summary_lbl   = NULL;

static lv_obj_t* s_screen = NULL;
static bool      s_built  = false;

// ── Helpers ──────────────────────────────────────────────

// Approximate line count: read in 4 KB blocks and count '\n'.
// We yield between blocks so the wardrive task doesn't starve
// while we scan a large CSV.
static uint32_t _count_lines(const char* path) {
    uint32_t lines = 0;
    PM_SPI_TAKE("wd_inspect_lines") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            uint8_t* buf = (uint8_t*)pm_psram_alloc(LINE_CHUNK);
            if (buf) {
                while (1) {
                    size_t n = pm_file_read(f, buf, LINE_CHUNK);
                    if (n == 0) break;
                    for (size_t i = 0; i < n; i++) {
                        if (buf[i] == '\n') lines++;
                    }
                    if (n < LINE_CHUNK) break;
                }
                pm_psram_free(buf);
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return lines;
}

// Match a filename against the patterns we care about. Returns the
// source category, or -1 if not interesting.
static int _classify(const char* dir, const char* name) {
    if (!name) return -1;
    if (strcmp(dir, "/sd/sessions") == 0) {
        size_t l = strlen(name);
        if (l >= 3 && strcmp(name + l - 3, ".db") == 0) return WI_SRC_SESSIONS_DB;
        return -1;
    }
    if (strcmp(dir, "/sd/exports") == 0) {
        if (strncmp(name, "wardrive_", 9) != 0) return -1;
        size_t l = strlen(name);
        if (l >= 4 && strcmp(name + l - 4, ".csv") == 0)  return WI_SRC_CSV_EXPORTS;
        if (l >= 5 && strcmp(name + l - 5, ".json") == 0) return WI_SRC_JSON_EXPORTS;
        return -1;
    }
    return -1;
}

// Format bytes as KB/MB with one decimal place.
static void _fmt_size(uint64_t b, char* out, size_t cap) {
    if (b < 1024)            { snprintf(out, cap, "%llu B",   (unsigned long long)b); return; }
    if (b < 1024 * 1024)     { snprintf(out, cap, "%.1f KB",  b / 1024.0); return; }
    if (b < 1024ULL * 1024 * 1024) {
        snprintf(out, cap, "%.1f MB", b / (1024.0 * 1024.0));
        return;
    }
    snprintf(out, cap, "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
}

// Health classification — green/amber/red.
typedef enum { HEALTH_GOOD = 0, HEALTH_THIN, HEALTH_EMPTY } health_t;
static health_t _health(const wi_file_t* f) {
    if (f->size_bytes == 0)                    return HEALTH_EMPTY;
    if (f->source == WI_SRC_SESSIONS_DB)       return f->size_bytes > 4096 ? HEALTH_GOOD : HEALTH_THIN;
    // CSV / JSON: ≤1 line is header-only or empty.
    if (f->line_count <= 1 || f->size_bytes < 256) return HEALTH_THIN;
    return HEALTH_GOOD;
}

static lv_color_t _health_color(health_t h) {
    switch (h) {
        case HEALTH_GOOD:  return PM_LAYOUT_COL_OK;
        case HEALTH_THIN:  return PM_LAYOUT_COL_WARN;
        case HEALTH_EMPTY: return PM_LAYOUT_COL_ERR;
    }
    return PM_LAYOUT_COL_DIM;
}

static const char* _health_label(health_t h) {
    switch (h) {
        case HEALTH_GOOD:  return "OK";
        case HEALTH_THIN:  return "THIN";
        case HEALTH_EMPTY: return "EMPTY";
    }
    return "?";
}

// Comparator: newest-first by name (filenames embed timestamps, so
// lexicographic newest works for our format).
static int _cmp_newest(const void* a, const void* b) {
    const wi_file_t* fa = (const wi_file_t*)a;
    const wi_file_t* fb = (const wi_file_t*)b;
    return strcmp(fb->name, fa->name);
}

// Scan one directory and append matching files to s_files.
static void _scan_dir(const char* path) {
    PM_SPI_TAKE("wd_inspect_scan_dir") {
        pm_dir_t* d = pm_dir_open(path);
        if (d) {
            const char* name;
            bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL &&
                   s_count < MAX_FILES) {
                if (is_dir) continue;
                int src = _classify(path, name);
                if (src < 0) continue;
                wi_file_t* e = &s_files[s_count++];
                memset(e, 0, sizeof(*e));
                e->source = (wi_src_t)src;
                strncpy(e->name, name, sizeof(e->name) - 1);
                snprintf(e->path, sizeof(e->path), "%s/%s", path, name);
                // Size — quick stat call. Some pm_file_size impls
                // require pm_file_open; do it cheaply if so.
                pm_file_t* f = pm_file_open(e->path, PM_FILE_READ);
                if (f) {
                    e->size_bytes = (uint64_t)pm_file_size(f);
                    pm_file_close(f);
                }
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
}

// Scan everything. CSVs need line counts — those open the file
// again under the mutex.
static void _scan(void) {
    if (s_scan_busy) return;
    s_scan_busy = true;
    s_count = 0;
    if (!s_files) {
        s_files = (wi_file_t*)pm_psram_alloc(sizeof(wi_file_t) * MAX_FILES);
        if (!s_files) {
            pm_log_e(TAG, "PSRAM alloc failed");
            s_scan_busy = false;
            return;
        }
    }
    memset(s_files, 0, sizeof(wi_file_t) * MAX_FILES);

    _scan_dir("/sd/exports");
    _scan_dir("/sd/sessions");

    // Sort newest first.
    if (s_count > 1) {
        qsort(s_files, s_count, sizeof(wi_file_t), _cmp_newest);
    }

    // Line counts for text files only — skip .db (cost not worth it).
    for (int i = 0; i < s_count; i++) {
        wi_file_t* e = &s_files[i];
        if (e->source == WI_SRC_SESSIONS_DB) continue;
        if (e->size_bytes == 0) continue;
        e->line_count = _count_lines(e->path);
        pm_delay_ms(1);   // yield between files
    }

    s_scan_busy = false;
}

// ── UI helpers ───────────────────────────────────────────
static const char* _src_short(wi_src_t s) {
    switch (s) {
        case WI_SRC_CSV_EXPORTS:  return "CSV";
        case WI_SRC_JSON_EXPORTS: return "JSON";
        case WI_SRC_SESSIONS_DB:  return "DB";
        default:                  return "?";
    }
}
static lv_color_t _src_color(wi_src_t s) {
    switch (s) {
        case WI_SRC_CSV_EXPORTS:  return PM_LAYOUT_COL_ACCENT;
        case WI_SRC_JSON_EXPORTS: return PM_LAYOUT_COL_PURPLE;
        case WI_SRC_SESSIONS_DB:  return PM_LAYOUT_COL_GOLD;
        default:                  return PM_LAYOUT_COL_DIM;
    }
}

// ── Row builder ──────────────────────────────────────────
static void _build_row(int idx, lv_obj_t* parent) {
    wi_row_ui_t* r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 32);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_column(r->row, 10, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    r->status_dot = lv_obj_create(r->row);
    lv_obj_remove_style_all(r->status_dot);
    lv_obj_set_size(r->status_dot, 10, 10);
    lv_obj_set_style_bg_color(r->status_dot, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_bg_opa(r->status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r->status_dot, LV_RADIUS_CIRCLE, 0);

    // Source badge (CSV / JSON / DB)
    lv_obj_t* src_chip = lv_obj_create(r->row);
    lv_obj_remove_style_all(src_chip);
    lv_obj_set_size(src_chip, 48, 18);
    lv_obj_set_style_bg_opa(src_chip, 30, 0);
    lv_obj_set_style_border_width(src_chip, 1, 0);
    lv_obj_set_style_radius(src_chip, 3, 0);
    lv_obj_clear_flag(src_chip, LV_OBJ_FLAG_SCROLLABLE);
    r->badge_lbl = lv_label_create(src_chip);
    lv_label_set_text(r->badge_lbl, "—");
    lv_obj_set_style_text_font(r->badge_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_center(r->badge_lbl);

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
    lv_obj_set_width(r->size_lbl, 90);
    lv_obj_set_style_text_align(r->size_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    r->rows_lbl = lv_label_create(r->row);
    lv_label_set_text(r->rows_lbl, "—");
    lv_obj_set_style_text_font(r->rows_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->rows_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->rows_lbl, 80);
    lv_obj_set_style_text_align(r->rows_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

// ── Render ───────────────────────────────────────────────
static void _render(void) {
    if (!s_built) return;

    uint64_t total_bytes = 0;
    uint64_t total_rows  = 0;
    int      healthy     = 0;

    for (int i = 0; i < s_count; i++) {
        const wi_file_t* f = &s_files[i];
        total_bytes += f->size_bytes;
        if (f->source != WI_SRC_SESSIONS_DB) total_rows += f->line_count;
        if (_health(f) == HEALTH_GOOD) healthy++;
    }

    // Stats
    if (s_stat_files)   { char b[12]; snprintf(b, sizeof(b), "%d", s_count); lv_label_set_text(s_stat_files, b); }
    if (s_stat_bytes)   { char b[24]; _fmt_size(total_bytes, b, sizeof(b)); lv_label_set_text(s_stat_bytes, b); }
    if (s_stat_rows) {
        char b[24];
        if (total_rows >= 1000000)  snprintf(b, sizeof(b), "%.1fM", total_rows / 1000000.0);
        else if (total_rows >= 1000) snprintf(b, sizeof(b), "%.1fK", total_rows / 1000.0);
        else                         snprintf(b, sizeof(b), "%llu", (unsigned long long)total_rows);
        lv_label_set_text(s_stat_rows, b);
    }
    if (s_stat_healthy) { char b[12]; snprintf(b, sizeof(b), "%d/%d", healthy, s_count); lv_label_set_text(s_stat_healthy, b); }

    // Status chip
    if (s_chip_status) {
        if (s_scan_busy)            lv_label_set_text(s_chip_status, "SCANNING");
        else if (s_count == 0)      lv_label_set_text(s_chip_status, "EMPTY");
        else                        lv_label_set_text(s_chip_status, "READY");
    }

    // Verdict summary line.
    if (s_summary_lbl) {
        if (s_count == 0) {
            lv_label_set_text(s_summary_lbl,
                "No wardrive files found. The Ghost Engine hasn't laid anything down yet.");
        } else if (healthy == s_count) {
            lv_label_set_text(s_summary_lbl,
                "All files look healthy. Rotation is working.");
        } else if (healthy == 0) {
            lv_label_set_text(s_summary_lbl,
                "No files have data rows. SD writes may be failing.");
        } else {
            char b[120];
            snprintf(b, sizeof(b),
                "%d of %d files healthy. Inspect the THIN/EMPTY rows for rotation issues.",
                healthy, s_count);
            lv_label_set_text(s_summary_lbl, b);
        }
    }

    // Rows
    int visible = 0;
    for (int i = 0; i < s_count && i < MAX_VISIBLE_ROWS; i++) {
        const wi_file_t* f = &s_files[i];
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        wi_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);

        health_t h = _health(f);
        lv_color_t hc = _health_color(h);
        lv_obj_set_style_bg_color(r->status_dot, hc, 0);

        lv_label_set_text(r->name_lbl, f->name);

        lv_color_t sc = _src_color(f->source);
        lv_label_set_text(r->badge_lbl, _src_short(f->source));
        lv_obj_set_style_text_color(r->badge_lbl, sc, 0);
        lv_obj_t* chip = lv_obj_get_parent(r->badge_lbl);
        if (chip) {
            lv_obj_set_style_border_color(chip, sc, 0);
            lv_obj_set_style_bg_color(chip, sc, 0);
        }

        char sb[24];
        _fmt_size(f->size_bytes, sb, sizeof(sb));
        lv_label_set_text(r->size_lbl, sb);

        char rb[24];
        if (f->source == WI_SRC_SESSIONS_DB) snprintf(rb, sizeof(rb), "%s", _health_label(h));
        else                                  snprintf(rb, sizeof(rb), "%lu lines", (unsigned long)f->line_count);
        lv_label_set_text(r->rows_lbl, rb);
        lv_obj_set_style_text_color(r->rows_lbl,
            (h == HEALTH_GOOD) ? PM_LAYOUT_COL_OK : hc, 0);

        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Actions ──────────────────────────────────────────────
static void _act_rescan(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "rescan");
    if (!pm_sd_mounted()) pm_sd_mount();
    _scan();
    _render();
}

// ── Screen build ─────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "WD INSPECT");

    s_chip_status = pm_app_layout_chip(&L, "READY", PM_LAYOUT_COL_OK);

    pm_app_layout_stats_row(&L, 4);
    s_stat_files   = pm_app_layout_stat(&L, "FILES",   "0");
    s_stat_bytes   = pm_app_layout_stat(&L, "TOTAL",   "0 B");
    s_stat_rows    = pm_app_layout_stat(&L, "ROWS",    "0");
    s_stat_healthy = pm_app_layout_stat(&L, "HEALTHY", "0/0");

    pm_app_layout_content(&L);

    lv_obj_t* pane = pm_app_layout_pane(&L, 0, "WARDRIVE OUTPUTS");

    s_list_box = lv_obj_create(pane);
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

    // Summary footer line inside the pane (under the scroll list).
    lv_obj_t* footer = lv_obj_create(pane);
    lv_obj_remove_style_all(footer);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, 24);
    lv_obj_set_style_bg_color(footer, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(footer, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_hor(footer, 10, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_summary_lbl = lv_label_create(footer);
    lv_label_set_text(s_summary_lbl, "Ready. Tap RESCAN to inspect SD.");
    lv_label_set_long_mode(s_summary_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_summary_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_summary_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_summary_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_align(s_summary_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    pm_app_layout_action(&L, "RESCAN", PM_LAYOUT_COL_ACCENT, _act_rescan);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ────────────────────────────────────────────
static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    if (!pm_sd_mounted()) pm_sd_mount();
    _scan();
    _render();
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
}

static void _deinit(void) {
    if (s_files) { pm_psram_free(s_files); s_files = NULL; }
}

static const pm_app_t _APP = {
    .id           = "wd_inspect",
    .display_name = "WD INSPECT",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_wardrive_inspect(void) { return &_APP; }
