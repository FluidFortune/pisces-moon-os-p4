// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_pkt_analysis.c — Offline packet analysis
//
//  Browses /sd/captures/*.pcap files. Parses pcap global
//  header, then iterates record headers + frame data. Decodes
//  802.11 management frames (frame_ctrl, addresses, IE list).
//
//  Skeleton parser — full per-IE decode is a follow-up. The
//  app surface (file picker, frame list, decode pane) is
//  ready for it.
// ============================================================

#include "pm_app_pkt_analysis.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_PKT_ANALYSIS";

#define CAPTURES_DIR "/sd/captures"
#define MAX_FILES    32
#define MAX_FRAMES   512
#define PATH_SIZE    96

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_global_t;

typedef struct __attribute__((packed)) {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_record_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t incl_len;
    int      offset;            // file offset to frame data
    uint8_t  frame_subtype;     // 0..15 (within mgmt class)
} frame_meta_t;

static char         s_files[MAX_FILES][PATH_SIZE];
static int          s_file_count = 0;
static int          s_file_cursor = 0;
static char         s_open_path[PATH_SIZE] = "";
static frame_meta_t s_frames[MAX_FRAMES];
static int          s_frame_count = 0;
static int          s_frame_cursor = 0;

static void _scan_files(void) {
    s_file_count = 0;
    PM_SPI_TAKE("pa_scan") {
        pm_dir_t* d = pm_dir_open(CAPTURES_DIR);
        if (d) {
            const char* n; bool is_dir;
            while ((n = pm_dir_next(d, &is_dir)) != NULL && s_file_count < MAX_FILES) {
                if (is_dir) continue;
                if (!strstr(n, ".pcap")) continue;
                snprintf(s_files[s_file_count++], PATH_SIZE,
                         "%s/%s", CAPTURES_DIR, n);
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
    pm_log_i(TAG, "%d capture files", s_file_count);
}

static void _open_file(const char* path) {
    s_frame_count = 0;
    strncpy(s_open_path, path, sizeof(s_open_path) - 1);
    PM_SPI_TAKE("pa_open") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            pcap_global_t gh;
            if (pm_file_read(f, &gh, sizeof(gh)) == sizeof(gh) &&
                (gh.magic == 0xA1B2C3D4 || gh.magic == 0xD4C3B2A1)) {
                int offset = sizeof(gh);
                while (s_frame_count < MAX_FRAMES) {
                    pcap_record_t rh;
                    if (pm_file_read(f, &rh, sizeof(rh)) != sizeof(rh)) break;
                    if (rh.incl_len == 0 || rh.incl_len > 65535) break;
                    frame_meta_t* m = &s_frames[s_frame_count++];
                    m->ts_sec   = rh.ts_sec;
                    m->incl_len = rh.incl_len;
                    m->offset   = offset + sizeof(rh);
                    // Peek frame_ctrl byte for subtype
                    uint8_t fc;
                    if (pm_file_read(f, &fc, 1) == 1) {
                        m->frame_subtype = (fc >> 4) & 0x0F;
                    }
                    pm_file_seek(f, offset + sizeof(rh) + rh.incl_len);
                    offset += sizeof(rh) + rh.incl_len;
                }
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    pm_log_i(TAG, "%d frames in %s", s_frame_count, path);
}

static void _open_at_cursor(void) {
    if (s_file_cursor < 0 || s_file_cursor >= s_file_count) return;
    _open_file(s_files[s_file_cursor]);
}

static void _render(void) {
    pm_log_d(TAG, "files=%d frames=%d open=%s",
             s_file_count, s_frame_count, s_open_path);
    // TODO_LVGL: split-pane — file list left, frame list center,
    //            decode pane right showing parsed addresses + IEs
    //            for s_frames[s_frame_cursor].
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("PKT ANLYS",
        "PKT ANLYS app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen); pm_log_i(TAG, "enter"); _scan_files(); }
static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "pkt_analysis",
    .display_name = "PKT ANLYS",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_pkt_analysis(void) {
    (void)_open_at_cursor;
    return &_APP;
}
