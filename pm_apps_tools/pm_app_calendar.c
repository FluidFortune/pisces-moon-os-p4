// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_calendar.c — Monthly calendar
//
//  Carries forward S3 calendar.cpp logic:
//    - days_in_month with leap-year rules
//    - first_day_of_month via Zeller's congruence
//    - today highlighted, weekend column tinted
//    - prev/next/today navigation
//
//  New on P4: per-date notes stored at /sd/cal/YYYY-MM-DD.txt.
//  Tap a date → loads its note into a side panel; edit/save
//  uses Notepad's save mechanism (SPI Treaty-wrapped).
// ============================================================

#include "pm_app_calendar.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_CAL";

static const char* MONTHS[] = {
    "JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"
};

// View state
static int s_view_year  = 2026;
static int s_view_month = 0;          // 0..11

// Today (resolved from pm_time_now or default)
static int s_today_year  = 2026;
static int s_today_month = 0;
static int s_today_day   = 1;

// Selected day for note panel (0 = none)
static int s_sel_day = 0;

// LVGL handles
static void* s_screen   = NULL;
static void* s_grid     = NULL;
static void* s_lbl_title = NULL;
static void* s_lbl_note  = NULL;

// ─────────────────────────────────────────────
//  Calendar math
// ─────────────────────────────────────────────
static int _days_in_month(int month, int year) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return dim[month];
}

// 0=Sun..6=Sat for the 1st of (month, year).
static int _first_dow(int month, int year) {
    int m = month + 1;
    int y = year;
    if (m < 3) { m += 12; y--; }
    int k = y % 100, j = y / 100;
    int h = (1 + (13*(m+1)/5) + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;
}

// ─────────────────────────────────────────────
//  Note I/O
// ─────────────────────────────────────────────
static void _note_path(int y, int m, int d, char* out, size_t cap) {
    snprintf(out, cap, "/sd/cal/%04d-%02d-%02d.txt", y, m + 1, d);
}

static void _load_selected_note(void) {
    if (s_sel_day == 0) {
        // TODO_LVGL: lv_label_set_text(s_lbl_note, "");
        return;
    }
    char path[64];
    _note_path(s_view_year, s_view_month, s_sel_day, path, sizeof(path));

    char buf[1024];
    int  got = 0;
    PM_SPI_TAKE("cal_note_load") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            got = (int)pm_file_read(f, buf, sizeof(buf) - 1);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    buf[got > 0 ? got : 0] = 0;
    // TODO_LVGL: lv_label_set_text(s_lbl_note, buf);
    (void)buf;
}

static bool _save_selected_note(const char* text) {
    if (s_sel_day == 0 || !text) return false;
    char path[64];
    _note_path(s_view_year, s_view_month, s_sel_day, path, sizeof(path));

    bool ok = false;
    PM_SPI_TAKE("cal_note_save") {
        pm_file_mkdir("/sd/cal");
        pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            size_t len = strlen(text);
            ok = (pm_file_write(f, text, len) == len);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    pm_log_i(TAG, "%s note: %s", ok ? "saved" : "save FAILED", path);
    return ok;
}

// Public hook for the note editor overlay.
void pm_app_calendar_save_note(const char* text) {
    _save_selected_note(text);
}

// ─────────────────────────────────────────────
//  Navigation
// ─────────────────────────────────────────────
static void _refresh_today(void) {
    pm_time_t t;
    if (pm_time_now(&t)) {
        s_today_year  = t.year;
        s_today_month = t.month - 1;
        s_today_day   = t.day;
    }
}

static void _render(void) {
    char title[40];
    snprintf(title, sizeof(title), "%s %d", MONTHS[s_view_month], s_view_year);
    // TODO_LVGL: lv_label_set_text(s_lbl_title, title);

    // TODO_LVGL: rebuild the day grid — 7 columns, 6 rows max.
    // Highlight today (today_year/month/day matches view).
    // Highlight s_sel_day with a different style.

    pm_log_d(TAG, "render %s — %d days, first dow %d",
             title, _days_in_month(s_view_month, s_view_year),
             _first_dow(s_view_month, s_view_year));
}

void pm_app_calendar_prev_month(void) {
    if (--s_view_month < 0) { s_view_month = 11; s_view_year--; }
    s_sel_day = 0;
    _render();
    _load_selected_note();
}

void pm_app_calendar_next_month(void) {
    if (++s_view_month > 11) { s_view_month = 0; s_view_year++; }
    s_sel_day = 0;
    _render();
    _load_selected_note();
}

void pm_app_calendar_today(void) {
    _refresh_today();
    s_view_year  = s_today_year;
    s_view_month = s_today_month;
    s_sel_day    = s_today_day;
    _render();
    _load_selected_note();
}

void pm_app_calendar_select_day(int day) {
    int n = _days_in_month(s_view_month, s_view_year);
    if (day < 1 || day > n) return;
    s_sel_day = day;
    _render();
    _load_selected_note();
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("CALENDAR",
        "CALENDAR app — UI ready");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _refresh_today();
    s_view_year  = s_today_year;
    s_view_month = s_today_month;
    s_sel_day    = s_today_day;
    _render();
    _load_selected_note();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "calendar",
    .display_name = "CALENDAR",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_calendar(void) { return &_APP; }
