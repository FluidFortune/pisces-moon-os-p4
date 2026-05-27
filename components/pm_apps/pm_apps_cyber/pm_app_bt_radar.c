// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_bt_radar.c — Session BT radar, RSSI heatmap
// ============================================================

#include "pm_app_bt_radar.h"
#include "pm_app_wardrive.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_cardputer_i2c.h"
#include "pm_peer.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_BT_RADAR";

#define HEATMAP_DEVICES 32
#define HEATMAP_HISTORY 60     // last N RSSI samples per device

typedef struct {
    char     mac[18];
    char     name[32];
    int8_t   rssi_history[HEATMAP_HISTORY];
    int      hist_idx;
    uint32_t last_seen_ms;
    bool     active;
} radar_dev_t;

static radar_dev_t s_devs[HEATMAP_DEVICES];
static bool        s_session_active = false;
static pm_peer_t*  s_ble_peer = NULL;
static bool        s_ble_peer_started = false;

static int _find_or_make(const char* mac) {
    for (int i = 0; i < HEATMAP_DEVICES; i++)
        if (s_devs[i].active && strcmp(s_devs[i].mac, mac) == 0) return i;
    int oldest = -1; uint32_t oldest_ms = 0xFFFFFFFFu;
    for (int i = 0; i < HEATMAP_DEVICES; i++) {
        if (!s_devs[i].active) { oldest = i; break; }
        if (s_devs[i].last_seen_ms < oldest_ms) {
            oldest_ms = s_devs[i].last_seen_ms;
            oldest = i;
        }
    }
    memset(&s_devs[oldest], 0, sizeof(radar_dev_t));
    s_devs[oldest].active = true;
    return oldest;
}

void pm_app_bt_radar_on_seen(const char* mac, const char* name, int rssi,
                              const char* addr_type, const char* mfg) {
    if (!mac || !s_session_active) return;
    int idx = _find_or_make(mac);
    radar_dev_t* d = &s_devs[idx];
    strncpy(d->mac, mac, sizeof(d->mac) - 1);
    if (name && name[0]) strncpy(d->name, name, sizeof(d->name) - 1);
    d->rssi_history[d->hist_idx] = (int8_t)rssi;
    d->hist_idx = (d->hist_idx + 1) % HEATMAP_HISTORY;
    d->last_seen_ms = pm_millis();
    // Also feed the wardrive logger
    pm_app_wardrive_on_ble(mac, name, rssi, addr_type, mfg);
}

void pm_app_bt_radar_start(void) { s_session_active = true; }
void pm_app_bt_radar_stop(void)  { s_session_active = false; }

static void _stop_ble_source(void) {
    if (!s_ble_peer) return;
    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (s_ble_peer_started &&
        (kind == PM_PEER_KIND_CARDPUTER_I2C || kind == PM_PEER_KIND_TBEAM_S3)) {
        pm_peer_call(s_ble_peer, "ble_scan_stop", NULL);
    }
    pm_peer_release(s_ble_peer);
    pm_log_i(TAG, "BLE source stopped on %s", pm_peer_name(s_ble_peer));
    s_ble_peer = NULL;
    s_ble_peer_started = false;
}

static void _start_ble_source(void) {
    if (s_ble_peer) return;
    s_ble_peer = pm_peer_find("ble_scan", PM_PEER_ROLE_EXCLUSIVE);
    if (!s_ble_peer) {
        pm_log_w(TAG, "no BLE scan peer available");
        return;
    }

    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (kind == PM_PEER_KIND_CARDPUTER_I2C) {
        int rc = pm_peer_call(s_ble_peer, "ble_scan_start", "\"active\":0");
        if (rc != 0) {
            pm_log_w(TAG, "Cardputer BLE start failed rc=%d", rc);
            pm_peer_release(s_ble_peer);
            s_ble_peer = NULL;
            return;
        }
        s_ble_peer_started = true;
    } else if (kind == PM_PEER_KIND_TBEAM_S3) {
        int rc = pm_peer_call(s_ble_peer, "ble_scan_start", NULL);
        if (rc != 0) {
            pm_log_w(TAG, "T-Beam BLE start failed rc=%d", rc);
            pm_peer_release(s_ble_peer);
            s_ble_peer = NULL;
            return;
        }
        s_ble_peer_started = true;
    }

    pm_log_i(TAG, "BLE source selected: %s", pm_peer_name(s_ble_peer));
}

static void _poll_external_ble(void) {
    if (!s_session_active || !s_ble_peer) return;
    if (pm_peer_kind(s_ble_peer) != PM_PEER_KIND_CARDPUTER_I2C) return;

    for (int i = 0; i < 12; i++) {
        pm_cardputer_i2c_ble_seen_t b = {0};
        esp_err_t err = pm_cardputer_i2c_ble_seen_pop(&b);
        if (err != ESP_OK || !b.available) break;
        pm_app_bt_radar_on_seen(b.mac, b.name, b.rssi, b.addr_type, b.mfg);
    }
}

static void _render(void) {
    // TODO_LVGL: heatmap grid, one row per device, RSSI history
    //            visualized as a sparkline. [START][STOP][EXPORT] buttons.
    pm_log_d(TAG, "session=%d", s_session_active);
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BT RADAR",
        "BT RADAR app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter"); s_session_active = true; _start_ble_source();
}
static void _exit_(void) { pm_log_i(TAG, "exit"); s_session_active = false; _stop_ble_source(); }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    _poll_external_ble();
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "bt_radar",
    .display_name = "BT RADAR",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_bt_radar(void) { return &_APP; }
