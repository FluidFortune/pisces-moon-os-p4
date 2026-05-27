// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



#include "pm_app_ble_gatt.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_GATT";

#define MAX_SVCS 16
#define MAX_CHRS 64

typedef struct {
    char uuid[40];
    int  handle_start, handle_end;
} svc_t;

typedef struct {
    char uuid[40];
    int  handle;
    char props[16];
    int  service_idx;
    char last_value[80];
} chr_t;

static svc_t s_svcs[MAX_SVCS];
static chr_t s_chrs[MAX_CHRS];
static int   s_svc_count = 0;
static int   s_chr_count = 0;
static char  s_target_mac[18] = "";
static bool  s_connected = false;

void pm_app_ble_gatt_on_service(const char* uuid, int hs, int he) {
    if (s_svc_count >= MAX_SVCS) return;
    svc_t* s = &s_svcs[s_svc_count++];
    strncpy(s->uuid, uuid ? uuid : "", sizeof(s->uuid) - 1);
    s->handle_start = hs;
    s->handle_end   = he;
}

void pm_app_ble_gatt_on_char(const char* uuid, int handle, const char* props) {
    if (s_chr_count >= MAX_CHRS) return;
    chr_t* c = &s_chrs[s_chr_count++];
    strncpy(c->uuid,  uuid  ? uuid  : "", sizeof(c->uuid)  - 1);
    strncpy(c->props, props ? props : "", sizeof(c->props) - 1);
    c->handle = handle;
    c->service_idx = -1;
    for (int i = 0; i < s_svc_count; i++) {
        if (handle >= s_svcs[i].handle_start && handle <= s_svcs[i].handle_end) {
            c->service_idx = i; break;
        }
    }
}

void pm_app_ble_gatt_on_value(int handle, const char* hex) {
    for (int i = 0; i < s_chr_count; i++) {
        if (s_chrs[i].handle == handle) {
            strncpy(s_chrs[i].last_value, hex ? hex : "", sizeof(s_chrs[i].last_value) - 1);
            return;
        }
    }
}

void pm_app_ble_gatt_on_connected(const char* mac) {
    s_connected = true;
    strncpy(s_target_mac, mac ? mac : "", sizeof(s_target_mac) - 1);
    pm_log_i(TAG, "connected to %s", s_target_mac);
}

void pm_app_ble_gatt_on_disconnected(void) {
    s_connected = false;
    s_target_mac[0] = 0;
    s_svc_count = s_chr_count = 0;
}

void pm_app_ble_gatt_connect(const char* mac) {
    if (!mac) return;
    char cmd[120];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"ble_connect\",\"mac\":\"%s\"}", mac);
    pm_c6_cmd_send_raw(cmd);
}

void pm_app_ble_gatt_read(int handle) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"ble_read\",\"handle\":%d}", handle);
    pm_c6_cmd_send_raw(cmd);
}

void pm_app_ble_gatt_write(int handle, const char* hex) {
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"ble_write\",\"handle\":%d,\"hex\":\"%s\"}",
             handle, hex ? hex : "");
    pm_c6_cmd_send_raw(cmd);
}

static void _render(void) {
    pm_log_d(TAG, "conn=%d svcs=%d chrs=%d", s_connected, s_svc_count, s_chr_count);
    // TODO_LVGL: target MAC field + [CONNECT], tree of services
    //            (expandable to chars), per-char [READ][WRITE] actions,
    //            value display.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("GATT XPLR",
        "GATT XPLR app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (s_connected) pm_c6_cmd_send_raw("{\"cmd\":\"ble_disconnect\"}");
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "ble_gatt",
    .display_name = "GATT XPLR",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_ble_gatt(void) { return &_APP; }
