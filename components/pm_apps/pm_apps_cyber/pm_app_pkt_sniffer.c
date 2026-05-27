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
#include "pm_cardputer_i2c.h"
#include "pm_peer.h"
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
static pm_peer_t* s_capture_peer = NULL;
static uint8_t s_capture_channel = 1;
static uint32_t s_last_hop_ms = 0;

// Filter — bitmask: 1=mgmt 2=data 4=ctrl
static uint8_t s_filter = 0xFF;

static const char* _frame_type_name(uint8_t type) {
    switch (type) {
        case 1: return "mgmt";
        case 2: return "data";
        case 3: return "ctrl";
        default: return "unk";
    }
}

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
    if (s_active) return;
    s_capture_peer = pm_peer_find("wifi_capture", PM_PEER_ROLE_EXCLUSIVE);
    if (!s_capture_peer) {
        pm_log_w(TAG, "no WiFi capture peer available");
        return;
    }

    s_capture_channel = 1;
    int rc = pm_peer_call(s_capture_peer, "promiscuous_start",
                          "\"channel\":1,\"filter\":255");
    if (rc != 0) {
        pm_log_w(TAG, "capture start failed on %s rc=%d",
                 pm_peer_name(s_capture_peer), rc);
        pm_peer_release(s_capture_peer);
        s_capture_peer = NULL;
        return;
    }

    s_active = true;
    pm_log_i(TAG, "capture started on %s", pm_peer_name(s_capture_peer));
}
static void _stop(void) {
    if (!s_active && !s_capture_peer) return;
    s_active = false;
    if (s_capture_peer) {
        pm_peer_call(s_capture_peer, "promiscuous_stop", NULL);
        pm_log_i(TAG, "capture stopped on %s", pm_peer_name(s_capture_peer));
        pm_peer_release(s_capture_peer);
        s_capture_peer = NULL;
    }
    s_capture_channel = 1;
    s_last_hop_ms = 0;
}

static void _hop_cardputer_channel(uint32_t now) {
    if (!s_active || !s_capture_peer) return;
    if (pm_peer_kind(s_capture_peer) != PM_PEER_KIND_CARDPUTER_I2C) return;
    if (now - s_last_hop_ms < 750) return;

    s_capture_channel++;
    if (s_capture_channel > 13) s_capture_channel = 1;

    char params[32];
    snprintf(params, sizeof(params), "\"channel\":%u", s_capture_channel);
    pm_peer_call(s_capture_peer, "wifi_set_channel", params);
    s_last_hop_ms = now;
}

static void _poll_cardputer_frames(void) {
    if (!s_active || !s_capture_peer) return;
    if (pm_peer_kind(s_capture_peer) != PM_PEER_KIND_CARDPUTER_I2C) return;

    for (int i = 0; i < 4; i++) {
        pm_cardputer_i2c_wifi_frame_t f = {0};
        esp_err_t err = pm_cardputer_i2c_wifi_frame_pop(&f);
        if (err != ESP_OK || !f.available) break;

        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 f.mac[0], f.mac[1], f.mac[2], f.mac[3], f.mac[4], f.mac[5]);
        pm_app_pkt_sniffer_on_pkt(_frame_type_name(f.frame_type), mac, f.rssi);
    }
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
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter"); _start();
}
static void _exit_(void) { pm_log_i(TAG, "exit"); _stop(); }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    _hop_cardputer_channel(now);
    _poll_cardputer_frames();
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
