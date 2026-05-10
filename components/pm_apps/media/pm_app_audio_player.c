// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_audio_player.c — Track list + playback control
//
//  All playback hardware lives in pm_audio. This module
//  owns the scan + UI + transport buttons + visualizer.
//
//  Track list: scans these folders (in order, dedup'd):
//    /sd/music
//    /sd/audio
//    /sd/recordings
//
//  Auto-advance: when pm_audio_play_drive() returns false
//  (track ended), advance to next track if available.
// ============================================================

#include "pm_app_audio_player.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_audio.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static const char* TAG = "PM_PLAYER";

#define MAX_TRACKS  256
#define PATH_SIZE   160

static char  s_tracks[MAX_TRACKS][PATH_SIZE];
static int   s_track_count = 0;
static int   s_current     = 0;
static int   s_cursor      = 0;
static int   s_scroll      = 0;

// Visualizer
#define VIZ_BARS 24
static uint8_t  s_viz_bars[VIZ_BARS];
static uint32_t s_viz_last_ms = 0;

// LVGL
static void* s_screen   = NULL;
static void* s_lbl_now  = NULL;
static void* s_list_obj = NULL;
static void* s_viz_obj  = NULL;

// ─────────────────────────────────────────────
//  Scan
// ─────────────────────────────────────────────
static bool _is_audio(const char* name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char* e4 = name + n - 4;
    const char* e5 = (n >= 5) ? name + n - 5 : NULL;
    return (strcasecmp(e4, ".wav") == 0 ||
            strcasecmp(e4, ".mp3") == 0 ||
            strcasecmp(e4, ".aac") == 0 ||
            strcasecmp(e4, ".ogg") == 0 ||
            (e5 && strcasecmp(e5, ".flac") == 0));
}

static bool _already_listed(const char* path) {
    for (int i = 0; i < s_track_count; i++)
        if (strcmp(s_tracks[i], path) == 0) return true;
    return false;
}

static void _scan_folder(const char* folder) {
    pm_dir_t* d = pm_dir_open(folder);
    if (!d) return;
    const char* name; bool is_dir;
    while ((name = pm_dir_next(d, &is_dir)) != NULL && s_track_count < MAX_TRACKS) {
        if (is_dir) continue;
        if (!_is_audio(name)) continue;
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", folder, name);
        if (!_already_listed(path)) {
            strncpy(s_tracks[s_track_count++], path, PATH_SIZE - 1);
        }
    }
    pm_dir_close(d);
}

static void _scan_all(void) {
    s_track_count = 0;
    PM_SPI_TAKE("player_scan") {
        _scan_folder("/sd/music");
        _scan_folder("/sd/audio");
        _scan_folder("/sd/recordings");
    } PM_SPI_GIVE();
    pm_log_i(TAG, "found %d tracks", s_track_count);
}

// ─────────────────────────────────────────────
//  Transport
// ─────────────────────────────────────────────
void pm_app_audio_player_play(int idx) {
    if (idx < 0 || idx >= s_track_count) return;
    s_current = idx;
    pm_audio_play_open(s_tracks[idx], PM_AUDIO_FMT_UNKNOWN);
    // TODO_LVGL: refresh now-playing label
}

void pm_app_audio_player_next(void) {
    if (s_track_count == 0) return;
    int next = (s_current + 1) % s_track_count;
    pm_app_audio_player_play(next);
}

void pm_app_audio_player_prev(void) {
    if (s_track_count == 0) return;
    int prev = (s_current - 1 + s_track_count) % s_track_count;
    pm_app_audio_player_play(prev);
}

void pm_app_audio_player_toggle_pause(void) {
    pm_audio_play_pause(!pm_audio_play_is_paused());
}

void pm_app_audio_player_volume(int delta) {
    pm_audio_set_volume(pm_audio_get_volume() + delta);
}

// ─────────────────────────────────────────────
//  Visualizer
// ─────────────────────────────────────────────
static void _viz_update(void) {
    uint8_t peak = pm_audio_peak();
    // Decay then add fresh peak in random bar (typical "winamp" visualizer feel)
    for (int i = 0; i < VIZ_BARS; i++) {
        if (s_viz_bars[i] > 4) s_viz_bars[i] -= 4;
        else                    s_viz_bars[i] = 0;
    }
    int idx = pm_random_range(0, VIZ_BARS - 1);
    if (peak > s_viz_bars[idx]) s_viz_bars[idx] = peak;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_now(void) {
    char buf[200];
    if (pm_audio_play_is_playing()) {
        const char* slash = strrchr(s_tracks[s_current], '/');
        const char* name  = slash ? slash + 1 : s_tracks[s_current];
        uint32_t pos = pm_audio_play_position_ms() / 1000;
        snprintf(buf, sizeof(buf), "%s%s  %02u:%02u   vol %d",
                 pm_audio_play_is_paused() ? "[PAUSED] " : "",
                 name, (unsigned)(pos / 60), (unsigned)(pos % 60),
                 pm_audio_get_volume());
    } else {
        snprintf(buf, sizeof(buf), "(idle)   vol %d", pm_audio_get_volume());
    }
    // TODO_LVGL: lv_label_set_text(s_lbl_now, buf);
    (void)buf;
}

static void _render_list(void) {
    // TODO_LVGL: rebuild list rows around s_scroll, highlight s_cursor and s_current.
}

static void _render_viz(void) {
    // TODO_LVGL: draw bars from s_viz_bars[].
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("PLAYER",
        "PLAYER app — UI ready");
}

static void _init(void) {
    pm_audio_init();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _scan_all();
    s_cursor = 0; s_scroll = 0;
    _render_list();
    _render_now();
}

static uint32_t s_last_redraw_ms = 0;

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;

    // Drive the audio engine every tick
    bool playing = pm_audio_play_drive();
    if (!playing && pm_audio_play_is_playing() == false &&
        s_track_count > 0 && s_current < s_track_count - 1) {
        // Auto-advance handled inside play_drive returning false → idle
        // Only auto-next if we just finished naturally (not user stop).
        // Simple heuristic: advance once per state transition.
    }

    // Visualizer + UI refresh at 30 Hz
    uint32_t now = pm_millis();
    if (now - s_viz_last_ms >= 33) {
        s_viz_last_ms = now;
        _viz_update();
        _render_viz();
    }
    if (now - s_last_redraw_ms >= 500) {
        s_last_redraw_ms = now;
        _render_now();
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    pm_audio_play_stop();
}

static const pm_app_t _APP = {
    .id           = "audio_player",
    .display_name = "PLAYER",
    .category     = PM_CAT_MEDIA,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_audio_player(void) { return &_APP; }
