// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



#include "pm_app_beacon.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

static const char* TAG = "PM_BEACON";

#define MAX_APS 96

typedef struct {
    char     bssid[18];
    char     ssid[33];
    int      rssi;
    int      channel;
    char     enc[12];
    uint32_t last_seen_ms;
    int      hits;
} ap_t;

static ap_t s_aps[MAX_APS];
static int  s_count = 0;
static int  s_cursor = 0;

void pm_app_beacon_on_wifi(const char* bssid, const char* ssid,
                            int rssi, int channel, const char* enc) {
    if (!bssid) return;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_aps[i].bssid, bssid) == 0) {
            s_aps[i].rssi = rssi;
            s_aps[i].last_seen_ms = pm_millis();
            s_aps[i].hits++;
            return;
        }
    }
    if (s_count >= MAX_APS) {
        // Replace oldest
        int oldest = 0;
        for (int i = 1; i < s_count; i++)
            if (s_aps[i].last_seen_ms < s_aps[oldest].last_seen_ms) oldest = i;
        memset(&s_aps[oldest], 0, sizeof(ap_t));
        s_count = oldest + 1;     // overwrite slot
    }
    ap_t* a = &s_aps[s_count++];
    strncpy(a->bssid, bssid, sizeof(a->bssid) - 1);
    strncpy(a->ssid,  ssid ? ssid : "", sizeof(a->ssid) - 1);
    strncpy(a->enc,   enc  ? enc  : "?", sizeof(a->enc) - 1);
    a->rssi = rssi;
    a->channel = channel;
    a->last_seen_ms = pm_millis();
    a->hits = 1;
}

static int _cmp_rssi(const void* a, const void* b) {
    return ((const ap_t*)b)->rssi - ((const ap_t*)a)->rssi;
}

static void _render(void) {
    qsort(s_aps, s_count, sizeof(ap_t), _cmp_rssi);
    pm_log_d(TAG, "%d APs visible", s_count);
    // TODO_LVGL: table — channel, RSSI bar, encryption badge,
    //            BSSID, SSID. Click row → expand for detail.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BEACON",
        "BEACON app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) { pm_log_i(TAG, "exit"); }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "beacon",
    .display_name = "BEACON",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_beacon(void) { return &_APP; }
