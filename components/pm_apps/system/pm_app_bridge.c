// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_bridge.c — Upstream Bridge control panel
//
//  Currently presents UI state and stream toggles. The actual
//  USB-CDC handler (P4 → Jennifer) is implemented in
//  pm_upstream_bridge component (TODO — not in Phase 2;
//  belongs alongside the C6 bridge as an OS-level service,
//  not an app).
//
//  Until pm_upstream_bridge exists, the toggles here are
//  cached state only, reflected immediately so the user can
//  see the panel respond. When the upstream bridge component
//  comes online, it will read these flags to gate event
//  emission.
// ============================================================

#include "pm_app_bridge.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_BRIDGE_APP";

static pm_bridge_streams_t s_streams = {
    .wifi = true, .ble = true, .probe = true, .pkt = false
};
static bool s_connected_upstream = false;
static char s_last_cmd[64] = "(none)";
static char s_last_event[64] = "(none)";

const pm_bridge_streams_t* pm_bridge_streams(void) { return &s_streams; }

void pm_bridge_set_streams(const pm_bridge_streams_t* s) {
    if (!s) return;
    s_streams = *s;
    pm_log_i(TAG, "streams: wifi=%d ble=%d probe=%d pkt=%d",
             s->wifi, s->ble, s->probe, s->pkt);
}

bool pm_bridge_is_connected(void)  { return s_connected_upstream; }
bool pm_bridge_is_streaming(void)  {
    return s_streams.wifi || s_streams.ble || s_streams.probe || s_streams.pkt;
}

// ─────────────────────────────────────────────
//  Hook for upstream bridge to call when state changes
// ─────────────────────────────────────────────
void pm_bridge_app_set_connected(bool yes) {
    s_connected_upstream = yes;
}

void pm_bridge_app_record_cmd(const char* cmd) {
    if (!cmd) return;
    strncpy(s_last_cmd, cmd, sizeof(s_last_cmd) - 1);
    s_last_cmd[sizeof(s_last_cmd) - 1] = 0;
}

void pm_bridge_app_record_event(const char* event) {
    if (!event) return;
    strncpy(s_last_event, event, sizeof(s_last_event) - 1);
    s_last_event[sizeof(s_last_event) - 1] = 0;
}

// ─────────────────────────────────────────────
//  Toggle handlers (LVGL events bind here)
// ─────────────────────────────────────────────
static void _toggle_wifi(void)  { s_streams.wifi  = !s_streams.wifi;  }
static void _toggle_ble(void)   { s_streams.ble   = !s_streams.ble;   }
static void _toggle_probe(void) { s_streams.probe = !s_streams.probe; }
static void _toggle_pkt(void)   { s_streams.pkt   = !s_streams.pkt;   }

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: status row (connected / streaming),
    //            4 toggle buttons (WIFI/BLE/PROBE/PKT),
    //            last command and last event text rows,
    //            GPS state mirrored from C6.
    pm_log_d(TAG, "bridge: up=%d wifi=%d ble=%d probe=%d pkt=%d",
             s_connected_upstream,
             s_streams.wifi, s_streams.ble, s_streams.probe, s_streams.pkt);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("BRIDGE",
        "BRIDGE app — UI ready");
}

static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _render();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "bridge",
    .display_name = "BRIDGE",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_bridge(void) {
    (void)_toggle_wifi; (void)_toggle_ble;
    (void)_toggle_probe; (void)_toggle_pkt;
    return &_APP;
}
