// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_about.c — About panel
//
//  Renders a static information screen with:
//    Title:    "PISCES MOON OS — P4"
//    Version:  PM_VERSION_STRING
//    Hardware: pm_chip_info()
//    Memory:   live heap + PSRAM totals
//    License:  AGPL-3.0-or-later
//    Credits:  Eric Becker / Fluid Fortune
//    URL:      fluidfortune.com
//
//  Refresh: tick() updates the live memory line every 1s.
// ============================================================

#include "pm_app_about.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_ABOUT";

// LVGL handles (stubbed)
static void* s_screen        = NULL;
static void* s_lbl_memory    = NULL;
static uint32_t s_last_refresh = 0;

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("ABOUT",
        "ABOUT app — UI ready");
}

static void _refresh_memory(void) {
    char buf[160];
    pm_chip_info_t info;
    pm_chip_info(&info);

    snprintf(buf, sizeof(buf),
             "HEAP: %u KB free   |   PSRAM: %u KB free / %u MB total\n"
             "Chip: %s rev %d, %d cores, flash %u MB",
             (unsigned)(pm_free_heap()        / 1024),
             (unsigned)(pm_psram_free_bytes() / 1024),
             (unsigned)(info.psram_bytes      / (1024 * 1024)),
             info.chip_name, info.revision, info.cores,
             (unsigned)(info.flash_bytes / (1024 * 1024)));

    // TODO_LVGL: lv_label_set_text(s_lbl_memory, buf);
    (void)buf;
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_last_refresh = pm_millis();
    _refresh_memory();
    // TODO_LVGL: lv_scr_load(s_screen);
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_refresh >= 1000) {
        _refresh_memory();
        s_last_refresh = now;
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    // TODO_LVGL: revert screen if needed; launcher will reload on its return.
}

static const pm_app_t _APP = {
    .id           = "about",
    .display_name = "ABOUT",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_about(void) { return &_APP; }
