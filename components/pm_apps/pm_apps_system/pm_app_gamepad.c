// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gamepad.c — BLE gamepad pairing & button test
//
//  Subscribes to pm_input. Shows live state of:
//    - BT connection (paired device name)
//    - Last button pressed
//    - D-pad direction
//
//  Pairing button sends "bt_pair_gamepad" to the C6 over the
//  bridge. The C6 scans, connects to the first BLE HID gamepad
//  it sees, then streams gamepad_event lines back. Those land
//  in pm_c6_bridge's dispatcher and get posted to pm_input,
//  where this app's subscriber picks them up.
//
//  The legacy pm_gamepad_state() / pm_gamepad_on_hid_* API
//  is preserved as no-op stubs for backward compat with any
//  caller that still references it.
// ============================================================

#include "pm_app_gamepad.h"
#include "pm_input.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_GAMEPAD_APP";

// ── State (legacy, kept for backward-compat callers) ────────
static pm_gamepad_state_t s_gp = { 0 };
const pm_gamepad_state_t* pm_gamepad_state(void) { return &s_gp; }
void pm_gamepad_on_hid_input(uint16_t b, int x, int y) {
    (void)b; (void)x; (void)y;
    // Stub — Phase 14 routes through pm_input instead.
}
void pm_gamepad_on_hid_connect   (const char* n) { (void)n; }
void pm_gamepad_on_hid_disconnect(void)            { }

// ── App screen ──────────────────────────────────────────────
static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_dev_lbl;
static lv_obj_t* s_last_btn_lbl;
static lv_obj_t* s_dpad_lbl;
static int       s_sub_token = -1;

static const char* _btn_name(uint32_t code) {
    switch (code) {
        case PM_BTN_A:      return "A";
        case PM_BTN_B:      return "B";
        case PM_BTN_X:      return "X";
        case PM_BTN_Y:      return "Y";
        case PM_BTN_L:      return "L";
        case PM_BTN_R:      return "R";
        case PM_BTN_SELECT: return "SELECT";
        case PM_BTN_START:  return "START";
        case PM_BTN_HOME:   return "HOME";
    }
    return "?";
}

static void _format_dpad(uint32_t bm, char* out, int n) {
    if (bm == 0) { snprintf(out, n, "neutral"); return; }
    snprintf(out, n, "%s%s%s%s",
             (bm & PM_DPAD_UP)    ? "UP "    : "",
             (bm & PM_DPAD_DOWN)  ? "DOWN "  : "",
             (bm & PM_DPAD_LEFT)  ? "LEFT "  : "",
             (bm & PM_DPAD_RIGHT) ? "RIGHT " : "");
}

static void _on_input(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e) return;

    if (e->kind == PM_INPUT_BUTTON && e->down) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Last button: %s", _btn_name(e->code));
        if (s_last_btn_lbl) lv_label_set_text(s_last_btn_lbl, buf);
    }
    else if (e->kind == PM_INPUT_DPAD) {
        char buf[40], parts[28];
        _format_dpad(e->code, parts, sizeof(parts));
        snprintf(buf, sizeof(buf), "D-pad: %s", parts);
        if (s_dpad_lbl) lv_label_set_text(s_dpad_lbl, buf);
    }
}

static void _pair_clicked(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "Sending bt_pair_gamepad to C6");
    pm_c6_cmd_send_raw("{\"cmd\":\"bt_pair_gamepad\"}\n");
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Scanning... press the controller's BLE-mode key combo");
}

static void _disconnect_clicked(lv_event_t* e) {
    (void)e;
    pm_c6_cmd_send_raw("{\"cmd\":\"bt_disconnect_gamepad\"}\n");
}

static void _tick(uint32_t ms) {
    (void)ms;
    if (!s_status_lbl || !s_dev_lbl) return;
    bool conn = pm_input_bt_gamepad_connected();
    lv_label_set_text(s_status_lbl, conn ? "Status: connected" : "Status: not connected");
    const char* name = pm_input_bt_gamepad_name();
    char buf[64];
    snprintf(buf, sizeof(buf), "Device: %s", name && name[0] ? name : "—");
    lv_label_set_text(s_dev_lbl, buf);
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "GAMEPAD", NULL, NULL);

    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl   = pm_ui_kv_row(card, "Status",  "not connected");
    s_dev_lbl      = pm_ui_kv_row(card, "Device",  "—");
    s_last_btn_lbl = pm_ui_kv_row(card, "Last button", "—");
    s_dpad_lbl     = pm_ui_kv_row(card, "D-pad",   "neutral");

    lv_obj_t* btn_row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    pm_ui_button(btn_row, "PAIR",       _pair_clicked,       NULL);
    pm_ui_button(btn_row, "DISCONNECT", _disconnect_clicked, NULL);

    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint,
        "8BitDo Zero 2: hold START+Y for ~3 seconds while powering on\n"
        "to enter BLE-HID mode (instead of Switch mode).");
    lv_obj_set_style_text_color(hint, PM_C_FG_DIM, 0);
}

static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_sub_token < 0) s_sub_token = pm_input_subscribe(_on_input, NULL);
}
static void _exit_(void) {
    if (s_sub_token >= 0) { pm_input_unsubscribe(s_sub_token); s_sub_token = -1; }
}

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

const pm_app_t* pm_app_gamepad(void) { return &_APP; }
