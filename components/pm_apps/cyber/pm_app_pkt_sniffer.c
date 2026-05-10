// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#include "pm_app_pkt_sniffer.h"
#include "pm_app_wardrive.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_PKT_SNIFF";

#define RECENT_FRAMES 64

typedef struct {
    char     frame_type[12];
    char     src[18];
    int      rssi;
    uint32_t ts_ms;
} frame_t;

static frame_t s_recent[RECENT_FRAMES];
static int     s_write    = 0;
static int     s_count    = 0;
static int     s_total    = 0;
static int     s_mgmt = 0, s_data = 0, s_ctrl = 0;
static bool    s_active   = false;

// Filter — bitmask: 1=mgmt 2=data 4=ctrl
static uint8_t s_filter = 0xFF;

void pm_app_pkt_sniffer_on_pkt(const char* frame_type, const char* src, int rssi) {
    if (!s_active || !frame_type) return;

    if (strcmp(frame_type, "mgmt") == 0) { if (!(s_filter & 1)) return; s_mgmt++; }
    else if (strcmp(frame_type, "data") == 0) { if (!(s_filter & 2)) return; s_data++; }
    else if (strcmp(frame_type, "ctrl") == 0) { if (!(s_filter & 4)) return; s_ctrl++; }

    s_total++;
    frame_t* f = &s_recent[s_write];
    strncpy(f->frame_type, frame_type, sizeof(f->frame_type) - 1);
    strncpy(f->src, src ? src : "", sizeof(f->src) - 1);
    f->rssi = rssi;
    f->ts_ms = pm_millis();
    s_write = (s_write + 1) % RECENT_FRAMES;
    if (s_count < RECENT_FRAMES) s_count++;

    pm_app_wardrive_on_pkt(frame_type, src, rssi);
}

void pm_app_pkt_sniffer_set_filter(uint8_t mask) { s_filter = mask; }

static void _start(void) {
    s_active = true;
    pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_start\"}");
}
static void _stop(void) {
    s_active = false;
    pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_stop\"}");
}

static void _render(void) {
    pm_log_d(TAG, "tot=%d mgmt=%d data=%d ctrl=%d", s_total, s_mgmt, s_data, s_ctrl);
    // TODO_LVGL: counters HUD, filter checkboxes (MGMT/DATA/CTRL),
    //            scrolling table of last RECENT_FRAMES, [START][STOP].
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("PKT SNIFF",
        "PKT SNIFF app — UI ready");
}
static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen); pm_log_i(TAG, "enter"); _start(); }
static void _exit_(void) { pm_log_i(TAG, "exit"); _stop(); }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "pkt_sniffer",
    .display_name = "PKT SNIFF",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_pkt_sniffer(void) { return &_APP; }
