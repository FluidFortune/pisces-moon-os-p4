// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_usb_ducky.c — Ducky Script over native P4 USB HID
//
//  P4 has native USB HID — no C6 round-trip needed. ESP-IDF
//  TinyUSB component provides a HID class device. Setting
//  up the descriptors is the bring-up TODO; the engine path
//  is generic.
// ============================================================

#include "pm_app_usb_ducky.h"
#include "pm_ducky_engine.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_USB_DUCKY";

#define SCRIPTS_DIR "/sd/ducky"
#define MAX_SCRIPTS 32
#define PATH_SIZE   96

static char  s_scripts[MAX_SCRIPTS][PATH_SIZE];
static int   s_count = 0;
static int   s_cursor = 0;
static char* s_payload = NULL;
static bool  s_armed = false;

// USB HID send (TODO: wire TinyUSB HID descriptors + queue)
static void _usb_send_string(const char* s) {
    pm_log_w(TAG, "[stub] STRING %.40s", s);
    // TODO: tud_hid_keyboard_report() per char with HID usage codes.
}
static void _usb_send_key(const char* k) {
    pm_log_w(TAG, "[stub] KEY %s", k);
    // TODO: parse modifier+key, emit HID report with chord.
}
static void _usb_delay(uint32_t ms)    { pm_delay_ms(ms); }
static void _usb_log(const char* line) { pm_log_d(TAG, "▸ %s", line); }

static const pm_ducky_iface_t _IFACE = {
    .send_string = _usb_send_string,
    .send_key    = _usb_send_key,
    .delay_ms    = _usb_delay,
    .log_line    = _usb_log,
};

static void _scan_scripts(void) {
    s_count = 0;
    PM_SPI_TAKE("ud_scan") {
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

void pm_app_usb_ducky_arm(void)    { s_armed = true; }
void pm_app_usb_ducky_disarm(void) { s_armed = false; }

void pm_app_usb_ducky_fire(void) {
    if (!s_armed)  { pm_log_w(TAG, "not armed — hold ARM first"); return; }
    if (!s_payload){ pm_log_w(TAG, "no payload loaded"); return; }
    pm_ducky_run(s_payload, &_IFACE);
    s_armed = false;
}

static void _render(void) {
    pm_log_d(TAG, "scripts=%d cursor=%d armed=%d", s_count, s_cursor, s_armed);
    // TODO_LVGL: scripts list, payload preview, [ARM] hold button,
    //            big [FIRE] button (only enabled while armed).
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("USB DUCKY",
        "USB DUCKY app — UI ready");
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
    s_armed = false;
}

static const pm_app_t _APP = {
    .id           = "usb_ducky",
    .display_name = "USB DUCKY",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_usb_ducky(void) { return &_APP; }
