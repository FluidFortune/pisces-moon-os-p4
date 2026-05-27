// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_ble_ducky.c — Ducky Script over BLE HID (via C6)
// ============================================================

#include "pm_app_ble_ducky.h"
#include "pm_ducky_engine.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_BLE_DUCKY";

#define SCRIPTS_DIR "/sd/ducky"
#define MAX_SCRIPTS 32
#define PATH_SIZE   96

static char  s_scripts[MAX_SCRIPTS][PATH_SIZE];
static int   s_count = 0;
static int   s_cursor = 0;
static char* s_payload = NULL;
static bool  s_running = false;

// Ducky callbacks
static void _ble_send_string(const char* s) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"hid_string\",\"text\":\"%s\"}", s);
    pm_c6_cmd_send_raw(cmd);
}
static void _ble_send_key(const char* k) {
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"hid_key\",\"key\":\"%s\"}", k);
    pm_c6_cmd_send_raw(cmd);
}
static void _ble_delay(uint32_t ms)    { pm_delay_ms(ms); }
static void _ble_log(const char* line) { pm_log_d(TAG, "▸ %s", line); }

static const pm_ducky_iface_t _IFACE = {
    .send_string = _ble_send_string,
    .send_key    = _ble_send_key,
    .delay_ms    = _ble_delay,
    .log_line    = _ble_log,
};

static void _scan_scripts(void) {
    s_count = 0;
    PM_SPI_TAKE("bd_scan") {
        pm_file_mkdir(SCRIPTS_DIR);
        pm_dir_t* d = pm_dir_open(SCRIPTS_DIR);
        if (d) {
            const char* n; bool is_dir;
            while ((n = pm_dir_next(d, &is_dir)) != NULL && s_count < MAX_SCRIPTS) {
                if (is_dir) continue;
                if (!strstr(n, ".txt")) continue;
                snprintf(s_scripts[s_count++], PATH_SIZE, "%s/%s", SCRIPTS_DIR, n);
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
}

static bool _load_at_cursor(void) {
    if (s_cursor < 0 || s_cursor >= s_count) return false;
    if (s_payload) { pm_psram_free(s_payload); s_payload = NULL; }
    PM_SPI_TAKE("bd_load") {
        pm_file_t* f = pm_file_open(s_scripts[s_cursor], PM_FILE_READ);
        if (f) {
            size_t sz = pm_file_size(f);
            s_payload = (char*)pm_psram_alloc(sz + 1);
            if (s_payload) {
                size_t got = pm_file_read(f, s_payload, sz);
                s_payload[got] = 0;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return s_payload != NULL;
}

void pm_app_ble_ducky_fire(void) {
    if (!s_payload) return;
    s_running = true;
    pm_c6_cmd_send_raw("{\"cmd\":\"hid_pair\"}");
    pm_ducky_run(s_payload, &_IFACE);
    s_running = false;
}

static void _render(void) {
    pm_log_d(TAG, "scripts=%d cursor=%d running=%d", s_count, s_cursor, s_running);
    // TODO_LVGL: script list, [LOAD] / [PAIR] / [FIRE] buttons,
    //            payload preview, hold-to-fire safety.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BLE DUCKY",
        "BLE DUCKY app — UI ready");
}
static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter"); _scan_scripts();
}
static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (s_payload) { pm_psram_free(s_payload); s_payload = NULL; }
    pm_c6_cmd_send_raw("{\"cmd\":\"hid_disconnect\"}");
}

static const pm_app_t _APP = {
    .id           = "ble_ducky",
    .display_name = "BLE DUCKY",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_ble_ducky(void) {
    (void)_load_at_cursor;
    return &_APP;
}
