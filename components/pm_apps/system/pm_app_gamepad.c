// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gamepad.c — 8BitDo Zero 2 setup panel
//
//  Mirrors the S3 gamepad app:
//    - Status panel: connected / not connected, device name,
//      heap stat, idle/active indicator.
//    - Button test grid: 13 buttons with live highlight as
//      they're pressed.
//    - Pairing hint:    "Hold SELECT+R-Shoulder 3s …"
//
//  Phase 2: handlers registered, but C6 hid_input events are
//  not yet emitted. Once C6 firmware supports BLE HID forwarding,
//  pm_c6_bridge will call into pm_gamepad_on_hid_input().
// ============================================================

#include "pm_app_gamepad.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_GAMEPAD";

static pm_gamepad_state_t s_gp = {0};

const pm_gamepad_state_t* pm_gamepad_state(void) { return &s_gp; }

void pm_gamepad_on_hid_connect(const char* device_name) {
    s_gp.connected = true;
    strncpy(s_gp.device_name, device_name ? device_name : "8BitDo Zero 2",
            sizeof(s_gp.device_name) - 1);
    s_gp.device_name[sizeof(s_gp.device_name) - 1] = 0;
    s_gp.last_input_ms = pm_millis();
    pm_log_i(TAG, "connected: %s", s_gp.device_name);
}

void pm_gamepad_on_hid_disconnect(void) {
    s_gp.connected     = false;
    s_gp.device_name[0]= 0;
    s_gp.buttons       = 0;
    pm_log_i(TAG, "disconnected");
}

void pm_gamepad_on_hid_input(uint16_t buttons, int x, int y) {
    s_gp.buttons       = buttons;
    s_gp.last_input_ms = pm_millis();
    (void)x; (void)y;     // joystick fields reserved
    // TODO_LVGL: trigger differential redraw of pressed/released buttons.
}

// ─────────────────────────────────────────────
//  Pair / disconnect actions
// ─────────────────────────────────────────────
static void _action_pair(void) {
    pm_c6_cmd_send_raw("{\"cmd\":\"hid_pair\",\"device\":\"8bitdo_zero2\"}");
    pm_log_i(TAG, "pair request sent to C6");
}

static void _action_disconnect(void) {
    pm_c6_cmd_send_raw("{\"cmd\":\"hid_disconnect\"}");
    pm_log_i(TAG, "disconnect request sent to C6");
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_status(void) {
    // TODO_LVGL: status panel — green dot / red dot, device name,
    //           "Active" vs "Idle Ns".
}

static void _render_buttons(void) {
    // TODO_LVGL: 13-button grid. Highlight pressed bits in s_gp.buttons.
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("GAMEPAD",
        "GAMEPAD app — UI ready");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter (gamepad %sconnected)",
             s_gp.connected ? "" : "not ");
    _render_status();
    _render_buttons();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    // Idle timer for status panel "Idle Ns" indicator
    _render_status();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "gamepad",
    .display_name = "GAMEPAD",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_gamepad(void) {
    (void)_action_pair; (void)_action_disconnect;
    return &_APP;
}
