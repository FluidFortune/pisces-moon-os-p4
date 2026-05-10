// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_system.c — System diagnostic panel
//
//  Live readouts:
//    - uptime
//    - HEAP / PSRAM free
//    - chip / flash info
//    - SD mount state, SD free space
//    - C6 connection state, last ready event firmware string
//    - SPI Treaty contention counter (if instrumented)
//    - wall clock (if synced)
//
//  Actions (button row):
//    [REBOOT]   — esp_restart()
//    [SD MOUNT] — pm_sd_mount() if not mounted
//    [PING C6]  — sends ping, displays response
//    [NVS ERASE] (long-press confirm) — wipes NVS, reboots
// ============================================================

#include "pm_app_system.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <stdio.h>
#include <string.h>

#include "nvs_flash.h"

static const char* TAG = "PM_SYSTEM";

// LVGL handles (stubbed)
static void* s_screen      = NULL;
static void* s_lbl_uptime  = NULL;
static void* s_lbl_memory  = NULL;
static void* s_lbl_storage = NULL;
static void* s_lbl_c6      = NULL;
static void* s_lbl_clock   = NULL;

static uint32_t s_last_refresh = 0;

// ─────────────────────────────────────────────
//  Action handlers (called from LVGL button events)
// ─────────────────────────────────────────────
static void _action_reboot(void) {
    pm_log_w(TAG, "user requested reboot");
    pm_delay_ms(250);
    pm_reboot();
}

static void _action_sd_mount(void) {
    pm_log_i(TAG, "user requested SD mount");
    bool ok = pm_sd_mount();
    pm_log_i(TAG, "SD mount: %s", ok ? "OK" : "FAILED");
}

static void _action_c6_ping(void) {
    pm_log_i(TAG, "C6 ping");
    pm_c6_cmd_ping();
}

static void _action_nvs_erase(void) {
    pm_log_w(TAG, "NVS ERASE requested — wiping & rebooting");
    nvs_flash_erase();
    pm_delay_ms(250);
    pm_reboot();
}

// ─────────────────────────────────────────────
//  Refresh
// ─────────────────────────────────────────────
static void _refresh(void) {
    char buf[192];

    // Uptime
    uint32_t up = pm_uptime_seconds();
    snprintf(buf, sizeof(buf), "UPTIME: %02u:%02u:%02u",
             up / 3600, (up / 60) % 60, up % 60);
    // TODO_LVGL: lv_label_set_text(s_lbl_uptime, buf);

    // Memory
    snprintf(buf, sizeof(buf),
             "HEAP %u KB free | PSRAM %u KB free / largest %u KB",
             (unsigned)(pm_free_heap()                / 1024),
             (unsigned)(pm_psram_free_bytes()         / 1024),
             (unsigned)(pm_psram_largest_free_block() / 1024));
    // TODO_LVGL: lv_label_set_text(s_lbl_memory, buf);

    // Storage
    snprintf(buf, sizeof(buf),
             "SD: %s   |   /fs (LittleFS): mounted",
             pm_sd_mounted() ? "mounted" : "absent");
    // TODO_LVGL: lv_label_set_text(s_lbl_storage, buf);

    // C6 link
    snprintf(buf, sizeof(buf),
             "C6: %s   |   wardrive=%s   ble=%s",
             pm_c6_connected         ? "ONLINE"  : "OFFLINE",
             pm_c6_wardrive_active   ? "active"  : "idle",
             pm_c6_ble_active        ? "active"  : "idle");
    // TODO_LVGL: lv_label_set_text(s_lbl_c6, buf);

    // Clock
    pm_time_t t;
    if (pm_time_now(&t)) {
        snprintf(buf, sizeof(buf),
                 "TIME: %04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.minute, t.second);
    } else {
        snprintf(buf, sizeof(buf), "TIME: not synced");
    }
    // TODO_LVGL: lv_label_set_text(s_lbl_clock, buf);

    (void)buf;
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("SYSTEM",
        "SYSTEM app — UI ready");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_last_refresh = pm_millis();
    _refresh();
    // TODO_LVGL: lv_scr_load(s_screen);
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_refresh >= 500) {
        _refresh();
        s_last_refresh = now;
    }
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "system",
    .display_name = "SYSTEM",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_system(void) { return &_APP; }
