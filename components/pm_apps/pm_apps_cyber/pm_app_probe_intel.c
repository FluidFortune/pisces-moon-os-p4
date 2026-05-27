// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_probe_intel.c — Probe request intelligence
//
//  Built for v1.1.1 on the S3 but never wired into the
//  launcher. Phase 8 lights it up. Two modes:
//
//    SCAN mode   — uses normal C6 wifi scan responses.
//                  Lower volume but works without monitor mode.
//    PROMISC     — capture probe-req management frames.
//                  High volume; needs C6 promiscuous_start.
//
//  Output: MAC → list of probed SSIDs. Devices broadcast their
//  preferred network names; the table is the picture.
// ============================================================

#include "pm_app_probe_intel.h"
#include "pm_app_wardrive.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_PROBE";

#define MAX_DEVS  64
#define MAX_PER_DEV 8

typedef struct {
    char     mac[18];
    int      rssi;
    int      ssid_count;
    char     ssids[MAX_PER_DEV][33];
    uint32_t last_seen_ms;
} pdev_t;

static pdev_t s_devs[MAX_DEVS];
static int    s_count = 0;

typedef enum { MODE_SCAN, MODE_PROMISC } mode_t;
static mode_t s_mode = MODE_SCAN;

void pm_app_probe_intel_on_probe(const char* mac, const char* ssid,
                                   int rssi, int count) {
    (void)count;
    if (!mac) return;
    int idx = -1;
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_devs[i].mac, mac) == 0) { idx = i; break; }
    if (idx < 0) {
        if (s_count >= MAX_DEVS) {
            // Replace oldest
            int oldest = 0;
            for (int i = 1; i < s_count; i++)
                if (s_devs[i].last_seen_ms < s_devs[oldest].last_seen_ms) oldest = i;
            memset(&s_devs[oldest], 0, sizeof(pdev_t));
            idx = oldest;
        } else {
            idx = s_count++;
            memset(&s_devs[idx], 0, sizeof(pdev_t));
        }
        strncpy(s_devs[idx].mac, mac, sizeof(s_devs[idx].mac) - 1);
    }
    pdev_t* d = &s_devs[idx];
    d->rssi = rssi;
    d->last_seen_ms = pm_millis();
    if (ssid && ssid[0]) {
        // Add unique SSID
        for (int i = 0; i < d->ssid_count; i++)
            if (strcmp(d->ssids[i], ssid) == 0) goto done;
        if (d->ssid_count < MAX_PER_DEV) {
            strncpy(d->ssids[d->ssid_count], ssid, sizeof(d->ssids[0]) - 1);
            d->ssid_count++;
        }
    }
done:
    pm_app_wardrive_on_probe(mac, ssid, rssi, 1);
}

void pm_app_probe_intel_set_mode(int mode) {
    s_mode = (mode_t)mode;
    if (s_mode == MODE_PROMISC)
        pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_start\","
                            "\"filter\":\"probe_req\"}");
    else
        pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_stop\"}");
}

static void _render(void) {
    pm_log_d(TAG, "mode=%d devs=%d", (int)s_mode, s_count);
    // TODO_LVGL: mode toggle (SCAN / PROMISC), table grouped by MAC
    //            with expandable SSID list per row, RSSI bar.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("PROBE INTL",
        "PROBE INTL app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (s_mode == MODE_PROMISC)
        pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_stop\"}");
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "probe_intel",
    .display_name = "PROBE INTL",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_probe_intel(void) { return &_APP; }
