// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_wifi_ducky.c — Remote ducky via captive AP
//
//  Asks the C6 to host a small captive AP. Web form submitted
//  by an operator's phone gets replayed as USB HID keystrokes
//  on the P4 (using the same engine as pm_app_usb_ducky).
//
//  Two transports must be glued:
//    1. C6 SoftAP + tiny HTTP server (TODO in C6 fw).
//    2. P4 USB HID (TODO — same as usb_ducky).
//
//  Form posts come back to P4 as "wifi_ducky_form" events
//  containing the requested text/key sequence.
// ============================================================

#include "pm_app_wifi_ducky.h"
#include "pm_ducky_engine.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_WIFI_DUCKY";

static bool s_ap_up = false;
static char s_ap_ssid[33] = "PiscesDucky";
static char s_ap_pass[64] = "letmeducky";

// USB HID send — same shape as usb_ducky (would share, but
// these apps have different lifecycles).
static void _send_string(const char* s) { pm_log_w(TAG, "[stub] STRING %.40s", s); }
static void _send_key   (const char* k) { pm_log_w(TAG, "[stub] KEY %s", k); }
static void _delay      (uint32_t ms)   { pm_delay_ms(ms); }
static void _log        (const char* l) { pm_log_d(TAG, "▸ %s", l); }

static const pm_ducky_iface_t _IFACE = {
    .send_string = _send_string,
    .send_key    = _send_key,
    .delay_ms    = _delay,
    .log_line    = _log,
};

void pm_app_wifi_ducky_on_form(const char* payload) {
    if (!payload) return;
    pm_log_i(TAG, "form payload received: %d bytes", (int)strlen(payload));
    pm_ducky_run(payload, &_IFACE);
}

static void _ap_up(void) {
    char cmd[160];
    snprintf(cmd, sizeof(cmd),
             "{\"cmd\":\"wifi_ducky_ap_start\",\"ssid\":\"%s\",\"pass\":\"%s\"}",
             s_ap_ssid, s_ap_pass);
    pm_c6_cmd_send_raw(cmd);
    s_ap_up = true;
}

static void _ap_down(void) {
    pm_c6_cmd_send_raw("{\"cmd\":\"wifi_ducky_ap_stop\"}");
    s_ap_up = false;
}

static void _render(void) {
    pm_log_d(TAG, "ap=%d ssid=%s", s_ap_up, s_ap_ssid);
    // TODO_LVGL: SSID/pass fields, [START AP] / [STOP AP] toggle,
    //            connected client count, last-payload log line.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("WIFI DUCKY",
        "WIFI DUCKY app — UI ready");
}
static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen); pm_log_i(TAG, "enter"); _ap_up(); }
static void _exit_(void) { pm_log_i(TAG, "exit"); _ap_down(); }

static const pm_app_t _APP = {
    .id           = "wifi_ducky",
    .display_name = "WIFI DUCKY",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_wifi_ducky(void) { return &_APP; }
