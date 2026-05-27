// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_bluetooth.c — BLE radar (consumer of C6 ble_seen)
//
//  The C6 always emits ble_seen events when its BLE scanner
//  is active (Ghost Engine default). pm_c6_bridge dispatches
//  every event to all registered consumers. This app dedups
//  by MAC, refreshes RSSI on each sighting, and prunes
//  devices that haven't been seen in 60s.
// ============================================================

#include "pm_app_bluetooth.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_BT";

#define MAX_DEVICES 128
#define MAC_LEN     18
#define NAME_LEN    32
#define ADDR_LEN    8
#define MFG_LEN     32
#define DEVICE_TTL_MS  60000

typedef enum { SORT_LAST_SEEN, SORT_RSSI, SORT_NAME } sort_t;

typedef struct {
    char     mac[MAC_LEN];
    char     name[NAME_LEN];
    int      rssi;
    char     addr_type[ADDR_LEN];
    char     mfg_preview[MFG_LEN];
    uint32_t last_seen_ms;
    int      hit_count;
} bt_dev_t;

static bt_dev_t s_devs[MAX_DEVICES];
static int      s_count = 0;
static sort_t   s_sort  = SORT_RSSI;

// LVGL
static void* s_screen = NULL;

// ─────────────────────────────────────────────
//  Consumer
// ─────────────────────────────────────────────
static int _find_or_make(const char* mac) {
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_devs[i].mac, mac) == 0) return i;
    if (s_count >= MAX_DEVICES) {
        // Replace oldest
        int oldest = 0;
        for (int i = 1; i < s_count; i++)
            if (s_devs[i].last_seen_ms < s_devs[oldest].last_seen_ms) oldest = i;
        memset(&s_devs[oldest], 0, sizeof(bt_dev_t));
        return oldest;
    }
    return s_count++;
}

void pm_app_bluetooth_on_seen(const char* mac, const char* name,
                                int rssi, const char* addr_type,
                                const char* mfg) {
    if (!mac || !mac[0]) return;
    int idx = _find_or_make(mac);
    bt_dev_t* d = &s_devs[idx];
    strncpy(d->mac, mac, sizeof(d->mac) - 1);
    if (name && name[0]) strncpy(d->name, name, sizeof(d->name) - 1);
    if (addr_type) strncpy(d->addr_type, addr_type, sizeof(d->addr_type) - 1);
    if (mfg && mfg[0]) strncpy(d->mfg_preview, mfg, sizeof(d->mfg_preview) - 1);
    d->rssi         = rssi;
    d->last_seen_ms = pm_millis();
    d->hit_count++;
}

// ─────────────────────────────────────────────
//  Prune + sort
// ─────────────────────────────────────────────
static int _cmp_rssi(const void* a, const void* b) {
    return ((const bt_dev_t*)b)->rssi - ((const bt_dev_t*)a)->rssi;
}

static int _cmp_last_seen(const void* a, const void* b) {
    return (int)(((const bt_dev_t*)b)->last_seen_ms -
                  ((const bt_dev_t*)a)->last_seen_ms);
}

static int _cmp_name(const void* a, const void* b) {
    return strcasecmp(((const bt_dev_t*)a)->name,
                       ((const bt_dev_t*)b)->name);
}

static void _prune_and_sort(void) {
    uint32_t now = pm_millis();
    for (int i = 0; i < s_count; ) {
        if (now - s_devs[i].last_seen_ms > DEVICE_TTL_MS) {
            s_devs[i] = s_devs[--s_count];
        } else {
            i++;
        }
    }
    int (*cmp)(const void*, const void*) =
        s_sort == SORT_RSSI      ? _cmp_rssi      :
        s_sort == SORT_NAME      ? _cmp_name      :
                                    _cmp_last_seen;
    qsort(s_devs, s_count, sizeof(bt_dev_t), cmp);
}

void pm_app_bluetooth_set_sort(int sort_idx) { s_sort = (sort_t)sort_idx; }

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    _prune_and_sort();
    pm_log_d(TAG, "%d devices (sort=%d)", s_count, (int)s_sort);
    // TODO_LVGL: header (count + sort tabs), scrollable list of rows
    //            (RSSI bar | MAC | name | type | mfg).
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BT RADAR",
#if CONFIG_BT_ENABLED
        "ESP-Hosted BLE radar is active; waiting for advertisements.");
#else
        "BT host is disabled in sdkconfig. ESP-Hosted HCI wiring is unavailable.");
#endif
}
static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter (have %d cached devices)", s_count);
    _render();
#if CONFIG_BT_ENABLED
    pm_ui_default_screen_set_status(s_default_screen,
        "ESP-Hosted BLE scanner active; nearby advertisements will appear here.");
#else
    pm_log_w(TAG, "Bluetooth host disabled; skipping legacy C6 BLE start");
    pm_ui_default_screen_set_status(s_default_screen,
        "Bluetooth host disabled: enable ESP-Hosted Bluedroid HCI before scanning.");
#endif
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 500) return;
    s_last_render_ms = now;
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

#include <stdlib.h>
#include <strings.h>

static const pm_app_t _APP = {
    .id           = "bluetooth",
    .display_name = "BT RADAR",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_bluetooth(void) { return &_APP; }
