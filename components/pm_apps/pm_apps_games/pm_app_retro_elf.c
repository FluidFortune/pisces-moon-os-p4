// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_retro_elf.c — Retro emulator pack browser
//
//  Reuses the same per-module manifest pattern as the SYSTEM
//  ELF browser, but filtered to /sd/elf/games/ and tagged
//  as games for browse/sort.
// ============================================================

#include "pm_app_retro_elf.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_RETRO";

#define MAX_MODULES 32

typedef struct {
    char name[64];
    char path[160];
    char system[16];     // "NES", "GB", "Atari", ...
} retro_t;

static retro_t s_modules[MAX_MODULES];
static int     s_count  = 0;
static int     s_cursor = 0;

static bool _ends_with(const char* s, const char* suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return false;
    return strcasecmp(s + ls - lf, suf) == 0;
}

static void _scan(void) {
    s_count = 0;
    PM_SPI_TAKE("retro_scan") {
        pm_dir_t* d = pm_dir_open("/sd/elf/games");
        if (d) {
            const char* name; bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL && s_count < MAX_MODULES) {
                if (is_dir) continue;
                if (!_ends_with(name, ".elf")) continue;
                retro_t* m = &s_modules[s_count++];
                memset(m, 0, sizeof(*m));
                strncpy(m->name, name, sizeof(m->name) - 1);
                snprintf(m->path, sizeof(m->path), "/sd/elf/games/%s", name);
                strncpy(m->system, "RETRO", sizeof(m->system) - 1);
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
    pm_log_i(TAG, "found %d retro modules", s_count);
}

static void _launch_selected(void) {
    if (s_cursor < 0 || s_cursor >= s_count) return;
    pm_log_w(TAG, "[stub] would launch %s — pm_elf_loader pending",
             s_modules[s_cursor].path);
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("RETRO",
        "RETRO app — UI ready");
}

static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter"); _scan();
}
static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "retro_elf",
    .display_name = "RETRO",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_retro_elf(void) {
    (void)_launch_selected;
    return &_APP;
}
