// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_input.c — fan-out dispatcher
//
//  Tiny: just a fixed-size subscriber array and a "post" entry
//  point. No threading wrinkle because all callers are on the
//  LVGL task or the C6 bridge task; LVGL serializes its own
//  events and the bridge task is single-threaded.
//
//  If a future subscriber wants to do heavy work it should
//  queue from the handler, not block here.
// ============================================================

#include "pm_input.h"
#include "pm_hal.h"
#include <string.h>

static const char* TAG = "PM_INPUT";

#define MAX_SUBS 8

typedef struct {
    pm_input_handler_t cb;
    void*              user;
    bool               active;
} sub_slot_t;

static sub_slot_t s_subs[MAX_SUBS];
static pm_input_event_t s_last;
static bool             s_have_last = false;

static bool s_bt_gp_conn  = false;
static bool s_bt_kb_conn  = false;
static char s_bt_gp_name[40] = "";
static char s_bt_kb_name[40] = "";

int pm_input_subscribe(pm_input_handler_t cb, void* user) {
    if (!cb) return -1;
    for (int i = 0; i < MAX_SUBS; i++) {
        if (!s_subs[i].active) {
            s_subs[i].cb     = cb;
            s_subs[i].user   = user;
            s_subs[i].active = true;
            return i;
        }
    }
    pm_log_w(TAG, "subscribe: no free slots");
    return -1;
}

void pm_input_unsubscribe(int token) {
    if (token < 0 || token >= MAX_SUBS) return;
    s_subs[token].active = false;
    s_subs[token].cb     = NULL;
    s_subs[token].user   = NULL;
}

void pm_input_post(const pm_input_event_t* e) {
    if (!e) return;
    s_last      = *e;
    s_have_last = true;
    for (int i = 0; i < MAX_SUBS; i++) {
        if (s_subs[i].active && s_subs[i].cb) {
            s_subs[i].cb(e, s_subs[i].user);
        }
    }
}

bool pm_input_last(pm_input_event_t* out) {
    if (!out || !s_have_last) return false;
    *out = s_last;
    return true;
}

bool pm_input_bt_gamepad_connected (void) { return s_bt_gp_conn; }
bool pm_input_bt_keyboard_connected(void) { return s_bt_kb_conn; }

void pm_input_set_bt_gamepad_connected(bool yes, const char* name) {
    s_bt_gp_conn = yes;
    if (name) { strncpy(s_bt_gp_name, name, sizeof(s_bt_gp_name) - 1);
                s_bt_gp_name[sizeof(s_bt_gp_name) - 1] = 0; }
    else s_bt_gp_name[0] = 0;
    pm_log_i(TAG, "BT gamepad %s%s%s",
             yes ? "connected" : "disconnected",
             s_bt_gp_name[0] ? ": " : "",
             s_bt_gp_name);
}

void pm_input_set_bt_keyboard_connected(bool yes, const char* name) {
    s_bt_kb_conn = yes;
    if (name) { strncpy(s_bt_kb_name, name, sizeof(s_bt_kb_name) - 1);
                s_bt_kb_name[sizeof(s_bt_kb_name) - 1] = 0; }
    else s_bt_kb_name[0] = 0;
    pm_log_i(TAG, "BT keyboard %s%s%s",
             yes ? "connected" : "disconnected",
             s_bt_kb_name[0] ? ": " : "",
             s_bt_kb_name);
}

const char* pm_input_bt_gamepad_name (void) { return s_bt_gp_name; }
const char* pm_input_bt_keyboard_name(void) { return s_bt_kb_name; }
