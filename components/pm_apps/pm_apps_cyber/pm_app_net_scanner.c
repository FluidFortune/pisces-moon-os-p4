// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



#include "pm_app_net_scanner.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_NETSCAN";

#define MAX_HOSTS 64

typedef struct {
    char ip[20];
    char mac[18];
    char hostname[40];
    int  latency_ms;
    bool active;
} host_t;

static host_t s_hosts[MAX_HOSTS];
static int    s_count = 0;
static bool   s_scanning = false;
static char   s_subnet[20] = "";

void pm_app_net_scanner_on_host(const char* ip, const char* mac,
                                  const char* hostname, int latency_ms) {
    if (!ip) return;
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_hosts[i].ip, ip) == 0) {
            s_hosts[i].latency_ms = latency_ms;
            return;
        }
    if (s_count >= MAX_HOSTS) return;
    host_t* h = &s_hosts[s_count++];
    h->active = true;
    strncpy(h->ip,       ip,                       sizeof(h->ip)       - 1);
    strncpy(h->mac,      mac      ? mac      : "", sizeof(h->mac)      - 1);
    strncpy(h->hostname, hostname ? hostname : "", sizeof(h->hostname) - 1);
    h->latency_ms = latency_ms;
}

void pm_app_net_scanner_on_done(int total, const char* subnet) {
    s_scanning = false;
    if (subnet) strncpy(s_subnet, subnet, sizeof(s_subnet) - 1);
    pm_log_i(TAG, "scan done: %d hosts on %s", total, s_subnet);
}

static void _start_scan(void) {
    s_count = 0; s_scanning = true;
    pm_c6_cmd_send_raw("{\"cmd\":\"net_scan\"}");
}

static void _render(void) {
    pm_log_d(TAG, "subnet=%s hosts=%d scanning=%d",
             s_subnet, s_count, s_scanning);
    // TODO_LVGL: subnet header, [SCAN] button, host table
    //            (ip, mac, hostname, latency), spinner while scanning.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("NET SCAN",
        "NET SCAN app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) { pm_log_i(TAG, "exit"); s_scanning = false; }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "net_scanner",
    .display_name = "NET SCAN",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_net_scanner(void) {
    (void)_start_scan;
    return &_APP;
}
