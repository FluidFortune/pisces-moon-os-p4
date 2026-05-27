// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_audio_recorder.c — Voice memo recorder
//
//  Logic-port of S3 audio_recorder.cpp. The codec/I2S details
//  are encapsulated in pm_audio. This module owns:
//    - filename auto-numbering
//    - recordings list
//    - peak meter render
//    - delete from list
//
//  Persistence: recordings live at /sd/recordings/rec_NNN.wav.
//  pm_audio_record_* writes the WAV header on start and
//  patches it on stop.
// ============================================================

#include "pm_app_audio_recorder.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_audio.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_RECORDER";

#define MAX_RECS  128
#define PATH_SIZE 96

#define REC_DIR   "/sd/recordings"

static char  s_recs[MAX_RECS][PATH_SIZE];
static int   s_rec_count = 0;
static int   s_cursor    = 0;
static int   s_scroll    = 0;

// LVGL handles
static void* s_screen     = NULL;
static void* s_lbl_status = NULL;
static void* s_meter      = NULL;
static void* s_list_obj   = NULL;

// ─────────────────────────────────────────────
//  Filename
// ─────────────────────────────────────────────
static int _next_rec_num(void) {
    int max_n = 0;
    PM_SPI_TAKE("rec_next_num") {
        pm_file_mkdir(REC_DIR);
        pm_dir_t* d = pm_dir_open(REC_DIR);
        if (d) {
            const char* name; bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL) {
                if (is_dir) continue;
                if (strncmp(name, "rec_", 4) != 0) continue;
                int n = atoi(name + 4);
                if (n > max_n) max_n = n;
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
    return max_n + 1;
}

static void _scan_recs(void) {
    s_rec_count = 0;
    PM_SPI_TAKE("rec_scan") {
        pm_dir_t* d = pm_dir_open(REC_DIR);
        if (d) {
            const char* name; bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL && s_rec_count < MAX_RECS) {
                if (is_dir) continue;
                if (!strstr(name, ".wav")) continue;
                snprintf(s_recs[s_rec_count++], PATH_SIZE, "%s/%s", REC_DIR, name);
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();

    // Bubble sort ascending by name (filenames embed sequence)
    for (int i = 0; i < s_rec_count - 1; i++) {
        for (int j = i + 1; j < s_rec_count; j++) {
            if (strcmp(s_recs[i], s_recs[j]) > 0) {
                char tmp[PATH_SIZE];
                strcpy(tmp,       s_recs[i]);
                strcpy(s_recs[i], s_recs[j]);
                strcpy(s_recs[j], tmp);
            }
        }
    }
}

// ─────────────────────────────────────────────
//  Toggle / delete
// ─────────────────────────────────────────────
void pm_app_audio_recorder_toggle(void) {
    if (pm_audio_record_is_recording()) {
        pm_audio_record_stop();
        _scan_recs();
        pm_log_i(TAG, "stopped");
        return;
    }
    int n = _next_rec_num();
    char path[PATH_SIZE];
    snprintf(path, sizeof(path), "%s/rec_%03d.wav", REC_DIR, n);
    if (pm_audio_record_start(path)) {
        pm_log_i(TAG, "recording: %s", path);
    }
}

void pm_app_audio_recorder_delete_at_cursor(void) {
    if (s_cursor < 0 || s_cursor >= s_rec_count) return;
    if (pm_audio_record_is_recording()) return;     // safety
    PM_SPI_TAKE("rec_delete") {
        pm_file_remove(s_recs[s_cursor]);
    } PM_SPI_GIVE();
    _scan_recs();
    if (s_cursor >= s_rec_count) s_cursor = s_rec_count - 1;
    if (s_cursor < 0) s_cursor = 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_status(void) {
    char buf[120];
    if (pm_audio_record_is_recording()) {
        uint32_t s = pm_audio_record_elapsed_ms() / 1000;
        snprintf(buf, sizeof(buf), "● REC  %02u:%02u  (%u KB)",
                 (unsigned)(s / 60), (unsigned)(s % 60),
                 (unsigned)(pm_audio_record_bytes() / 1024));
    } else {
        snprintf(buf, sizeof(buf), "READY  %d recordings", s_rec_count);
    }
    // TODO_LVGL: lv_label_set_text(s_lbl_status, buf);
    (void)buf;
}

static void _render_meter(void) {
    uint8_t peak = pm_audio_record_peak();
    (void)peak;
    // TODO_LVGL: draw 32-segment LED meter, green→amber→red, with peak hold.
}

static void _render_list(void) {
    // TODO_LVGL: scrollable list, cursor highlight, file size column.
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("RECORDER",
        "RECORDER app — UI ready");
}

static void _init(void) {
    pm_audio_init();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _scan_recs();
    if (s_rec_count > 0) s_cursor = s_rec_count - 1;
    _render_status();
    _render_list();
}

static uint32_t s_last_meter_ms = 0;
static uint32_t s_last_status_ms = 0;

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (pm_audio_record_is_recording()) pm_audio_record_drive();

    uint32_t now = pm_millis();
    if (now - s_last_meter_ms >= 50) {
        s_last_meter_ms = now;
        _render_meter();
    }
    if (now - s_last_status_ms >= 250) {
        s_last_status_ms = now;
        _render_status();
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (pm_audio_record_is_recording()) pm_audio_record_stop();
}

static const pm_app_t _APP = {
    .id           = "audio_recorder",
    .display_name = "RECORDER",
    .category     = PM_CAT_MEDIA,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_audio_recorder(void) { return &_APP; }
