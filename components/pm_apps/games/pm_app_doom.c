// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_doom.c — DOOM (scaffold)
//
//  Status: app slot reserved, WAD-loading path stubbed.
//  Engine port is a separate, larger effort (chocolate-doom
//  → ESP-IDF, framebuffer to LVGL).
//
//  When loaded:
//    1. Look for /gamedata/doom.wad first (built-in partition).
//    2. Fall back to /sd/doom/doom.wad.
//    3. Allocate framebuffer (320×200 RGB565 = 128KB) in PSRAM.
//    4. Run engine ticks at 35 Hz (Doom's native).
// ============================================================

#include "pm_app_doom.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>

static const char* TAG = "PM_DOOM";

#define DOOM_W   320
#define DOOM_H   200
#define DOOM_BPP 2     /* RGB565 */

static uint8_t* s_framebuf  = NULL;
static char     s_wad_path[64] = "";
static bool     s_wad_loaded = false;

// LVGL
static void* s_screen = NULL;
static void* s_canvas = NULL;

// ─────────────────────────────────────────────
//  Resource discovery
// ─────────────────────────────────────────────
static bool _find_wad(void) {
    const char* candidates[] = {
        "/gamedata/doom.wad",
        "/gamedata/doom1.wad",
        "/sd/doom/doom.wad",
        "/sd/doom/doom1.wad",
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        if (pm_file_exists(candidates[i])) {
            strncpy(s_wad_path, candidates[i], sizeof(s_wad_path) - 1);
            return true;
        }
    }
    return false;
}

static bool _alloc_framebuf(void) {
    if (s_framebuf) return true;
    size_t sz = DOOM_W * DOOM_H * DOOM_BPP;
    s_framebuf = (uint8_t*)pm_psram_alloc(sz);
    if (!s_framebuf) {
        pm_log_e(TAG, "framebuf alloc %u bytes failed", (unsigned)sz);
        return false;
    }
    memset(s_framebuf, 0, sz);
    return true;
}

// ─────────────────────────────────────────────
//  Engine bridge — stub
// ─────────────────────────────────────────────
static void _engine_init(void) {
    // TODO: chocolate-doom port adapted to ESP-IDF would call
    //       D_DoomMain() variants here, with file I/O routed
    //       through pm_file_*. The work is substantial.
    pm_log_w(TAG, "doom engine not yet ported — scaffold only");
}

static void _engine_tick(void) {
    // TODO: D_DoomLoop() one iteration
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("DOOM",
        "DOOM app — UI ready");
}

static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    if (!_alloc_framebuf()) return;
    s_wad_loaded = _find_wad();
    if (s_wad_loaded) {
        pm_log_i(TAG, "WAD: %s", s_wad_path);
        _engine_init();
    } else {
        pm_log_w(TAG, "no WAD found at /gamedata/doom.wad or /sd/doom/");
    }
}

static uint32_t s_last_tick_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (!s_wad_loaded) return;
    uint32_t now = pm_millis();
    if (now - s_last_tick_ms < 28) return;     // ~35 Hz
    s_last_tick_ms = now;
    _engine_tick();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_framebuf) { pm_psram_free(s_framebuf); s_framebuf = NULL; }
}

static const pm_app_t _APP = {
    .id           = "doom",
    .display_name = "DOOM",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_doom(void) { return &_APP; }
