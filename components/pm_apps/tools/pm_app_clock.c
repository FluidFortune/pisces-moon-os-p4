// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_clock.c — Big digital clock + stopwatch
//
//  Display priority:
//    - If pm_time_now() returns synced=true → wall clock.
//    - Else → uptime in HH:MM:SS.
//
//  Stopwatch:
//    SPACE → start / pause
//    R     → reset
//
//  The S3 version did NTP on entry. P4 lets time sync happen
//  elsewhere (BSP / time service) and just reads pm_time_now().
//  GPS time, when received from the C6, can be applied via
//  settimeofday() — that wiring belongs in the C6 bridge GPS
//  callback, not here.
// ============================================================

#include "pm_app_clock.h"
#include "pm_hal.h"
#include <stdio.h>

static const char* TAG = "PM_CLOCK";

static bool     s_sw_running = false;
static bool     s_sw_started = false;
static uint64_t s_sw_start_us = 0;
static uint64_t s_sw_elapsed_us = 0;

static uint32_t s_last_redraw = 0;

// LVGL handles
static void* s_screen        = NULL;
static void* s_lbl_time      = NULL;
static void* s_lbl_date      = NULL;
static void* s_lbl_stopwatch = NULL;
static void* s_lbl_status    = NULL;

// ─────────────────────────────────────────────
//  Stopwatch input
// ─────────────────────────────────────────────
void pm_app_clock_sw_toggle(void) {
    uint64_t now = pm_micros();
    if (!s_sw_started) {
        s_sw_start_us   = now;
        s_sw_started    = true;
        s_sw_running    = true;
        s_sw_elapsed_us = 0;
    } else {
        s_sw_running = !s_sw_running;
        if (s_sw_running) s_sw_start_us = now - s_sw_elapsed_us;
    }
    pm_log_i(TAG, "stopwatch: %s", s_sw_running ? "running" : "paused");
}

void pm_app_clock_sw_reset(void) {
    s_sw_running    = false;
    s_sw_started    = false;
    s_sw_elapsed_us = 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static const char* DOW[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char* MON[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                              "JUL","AUG","SEP","OCT","NOV","DEC"};

static int _day_of_week(int y, int m, int d) {
    // Zeller's congruence
    if (m < 3) { m += 12; y--; }
    int k = y % 100, j = y / 100;
    int h = (d + (13*(m+1)/5) + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;     // 0=Sun..6=Sat
}

// ─────────────────────────────────────────────
//  LVGL handles
// ─────────────────────────────────────────────
#include "lvgl.h"
#include "pm_ui.h"

static lv_obj_t* s_lbl_time   = NULL;
static lv_obj_t* s_lbl_date   = NULL;
static lv_obj_t* s_lbl_status = NULL;
static lv_obj_t* s_lbl_sw     = NULL;

static void _render_time(void) {
    if (!s_lbl_time) return;
    char time_str[16];
    char date_str[40];
    char status[64];

    pm_time_t t;
    if (pm_time_now(&t)) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                 t.hour, t.minute, t.second);
        int dow = _day_of_week(t.year, t.month, t.day);
        snprintf(date_str, sizeof(date_str), "%s %s %02d %04d  UTC",
                 DOW[dow], MON[t.month - 1], t.day, t.year);
        snprintf(status, sizeof(status), "TIME SOURCE: synced");
    } else {
        uint32_t up = pm_uptime_seconds();
        snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
                 up / 3600, (up / 60) % 60, up % 60);
        snprintf(date_str, sizeof(date_str), "UPTIME (no sync — needs WiFi or GPS)");
        snprintf(status, sizeof(status), "TIME SOURCE: uptime fallback");
    }
    lv_label_set_text(s_lbl_time,   time_str);
    lv_label_set_text(s_lbl_date,   date_str);
    lv_label_set_text(s_lbl_status, status);
}

static void _render_stopwatch(void) {
    if (!s_lbl_sw) return;
    if (s_sw_running) s_sw_elapsed_us = pm_micros() - s_sw_start_us;
    uint64_t ms = s_sw_elapsed_us / 1000ULL;
    uint64_t s  = ms / 1000ULL;
    char buf[40];
    snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu.%02llu",
             (unsigned long long)(s / 3600ULL),
             (unsigned long long)((s / 60ULL) % 60ULL),
             (unsigned long long)(s % 60ULL),
             (unsigned long long)((ms % 1000ULL) / 10ULL));
    lv_label_set_text(s_lbl_sw, buf);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_screen_obj = NULL;

static void _sw_btn_cb(lv_event_t* e) {
    char* which = (char*)lv_event_get_user_data(e);
    if (which && which[0] == 'T') pm_app_clock_sw_toggle();
    if (which && which[0] == 'R') pm_app_clock_sw_reset();
}

static void _build_screen(void) {
    s_screen_obj = pm_ui_screen();
    pm_ui_titlebar(s_screen_obj, "CLOCK", NULL, NULL);

    // Time card — big
    lv_obj_t* time_card = pm_ui_card(s_screen_obj);
    lv_obj_set_flex_align(time_card, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_lbl_time = lv_label_create(time_card);
    lv_label_set_text(s_lbl_time, "--:--:--");
    lv_obj_set_style_text_color(s_lbl_time, PM_C_ACCENT, 0);
    lv_obj_set_style_text_font (s_lbl_time, &lv_font_montserrat_48, 0);

    s_lbl_date = lv_label_create(time_card);
    lv_label_set_text(s_lbl_date, "");
    lv_obj_set_style_text_color(s_lbl_date, PM_C_FG_DIM, 0);

    s_lbl_status = lv_label_create(time_card);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_color(s_lbl_status, PM_C_FG_DIM, 0);

    // Stopwatch card
    lv_obj_t* sw_card = pm_ui_card(s_screen_obj);
    lv_obj_t* sw_lbl = lv_label_create(sw_card);
    lv_label_set_text(sw_lbl, "STOPWATCH");
    lv_obj_set_style_text_color(sw_lbl, PM_C_FG_DIM, 0);

    s_lbl_sw = lv_label_create(sw_card);
    lv_label_set_text(s_lbl_sw, "00:00:00.00");
    lv_obj_set_style_text_color(s_lbl_sw, PM_C_FG, 0);
    lv_obj_set_style_text_font (s_lbl_sw, &lv_font_montserrat_28, 0);

    lv_obj_t* btn_row = lv_obj_create(sw_card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    pm_ui_button(btn_row, "Start/Stop", _sw_btn_cb, (void*)"T");
    pm_ui_button(btn_row, "Reset",      _sw_btn_cb, (void*)"R");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    pm_log_i(TAG, "enter");
    s_last_redraw = pm_millis();
    if (s_screen_obj) lv_screen_load(s_screen_obj);
    _render_time();
    _render_stopwatch();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    // 4 Hz redraw — clock at 1 Hz is too slow, 60 Hz is wasteful
    if (now - s_last_redraw < 250) return;
    s_last_redraw = now;
    _render_time();
    _render_stopwatch();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "clock",
    .display_name = "CLOCK",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_clock(void) { return &_APP; }
