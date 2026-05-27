// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_about.c — About panel
//
//  Renders a static information screen with version, hardware,
//  live memory (refreshed every 1s), license, credits, URL.
// ============================================================

#include "pm_app_about.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_ABOUT";

static lv_obj_t* s_screen     = NULL;
static lv_obj_t* s_v_heap     = NULL;
static lv_obj_t* s_v_psram    = NULL;
static lv_obj_t* s_v_uptime   = NULL;
static lv_obj_t* s_v_chip     = NULL;
static lv_obj_t* s_v_flash    = NULL;
static lv_obj_t* s_v_cores    = NULL;
static uint32_t  s_last_refresh = 0;

static void _refresh(void) {
    char buf[64];
    pm_chip_info_t info; pm_chip_info(&info);

    if (s_v_heap) {
        snprintf(buf, sizeof(buf), "%u KB",
                 (unsigned)(pm_free_heap() / 1024));
        lv_label_set_text(s_v_heap, buf);
    }
    if (s_v_psram) {
        snprintf(buf, sizeof(buf), "%u KB",
                 (unsigned)(pm_psram_free_bytes() / 1024));
        lv_label_set_text(s_v_psram, buf);
    }
    if (s_v_uptime) {
        uint32_t s = pm_uptime_seconds();
        snprintf(buf, sizeof(buf), "%uh %um", (unsigned)(s / 3600),
                                              (unsigned)((s / 60) % 60));
        lv_label_set_text(s_v_uptime, buf);
    }
    if (s_v_chip) {
        snprintf(buf, sizeof(buf), "%s rev %d", info.chip_name, info.revision);
        lv_label_set_text(s_v_chip, buf);
    }
    if (s_v_cores) {
        snprintf(buf, sizeof(buf), "%d", info.cores);
        lv_label_set_text(s_v_cores, buf);
    }
    if (s_v_flash) {
        snprintf(buf, sizeof(buf), "%u MB",
                 (unsigned)(info.flash_bytes / (1024 * 1024)));
        lv_label_set_text(s_v_flash, buf);
    }
}

static void _build_screen(void) {
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "PISCES MOON OS");
    pm_app_layout_chip(&L, "AGPL-3.0", PM_LAYOUT_COL_ACCENT);
    pm_app_layout_chip(&L, "v1.2.0-alpha", PM_LAYOUT_COL_OK);

    pm_app_layout_stats_row(&L, 6);
    s_v_heap   = pm_app_layout_stat(&L, "HEAP FREE",  "—");
    s_v_psram  = pm_app_layout_stat(&L, "PSRAM FREE", "—");
    s_v_uptime = pm_app_layout_stat(&L, "UPTIME",     "—");
    s_v_chip   = pm_app_layout_stat(&L, "CHIP",       "—");
    s_v_cores  = pm_app_layout_stat(&L, "CORES",      "—");
    s_v_flash  = pm_app_layout_stat(&L, "FLASH",      "—");

    pm_app_layout_content(&L);

    // Single wide pane for credits/license
    lv_obj_t* pane = pm_app_layout_pane(&L, 0, "PISCES MOON OS — P4");
    lv_obj_t* col = lv_obj_create(pane);
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(col, 20, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(col, 12, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);

    static const char* lines[] = {
        "A modular operating system for ESP32-class hardware.",
        "",
        "Reference chassis: ELECROW CrowPanel Advanced 7\" (ESP32-P4)",
        "Coprocessor: ESP32-C6 Ghost Engine (ESP-Hosted over SDIO)",
        "Display: 1024x600 MIPI-DSI, GT911 capacitive touch",
        "",
        "Eric Becker / Fluid Fortune",
        "fluidfortune.com",
        "",
        "Licensed under AGPL-3.0-or-later.",
        "Source available on GitHub.",
    };
    for (size_t i = 0; i < sizeof(lines)/sizeof(lines[0]); i++) {
        lv_obj_t* l = lv_label_create(col);
        lv_label_set_text(l, lines[i]);
        lv_obj_set_width(l, LV_PCT(100));
        lv_obj_set_style_text_font(l, PM_LAYOUT_FONT_TEXT, 0);
        bool dim = (lines[i][0] == 0 || strstr(lines[i], "Licensed")
                                     || strstr(lines[i], "Source"));
        lv_obj_set_style_text_color(l,
            dim ? PM_LAYOUT_COL_DIM : PM_LAYOUT_COL_FG, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }

    s_screen = pm_app_layout_end(&L);
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (!s_screen) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    s_last_refresh = pm_millis();
    _refresh();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_refresh >= 1000) {
        _refresh();
        s_last_refresh = now;
    }
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

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
