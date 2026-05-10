// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#include "pm_app_rf_spectrum.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_RF_SPEC";

#define NUM_CH      14
#define HISTORY_LEN 60     // 60 samples for waterfall

typedef struct {
    int rssi_floor;
    int util_pct;
    int8_t history[HISTORY_LEN];
    int    hist_idx;
} ch_t;

static ch_t s_channels[NUM_CH];
static bool s_sweeping = false;

void pm_app_rf_spectrum_on_channel(int channel, int rssi_floor, int util_pct) {
    if (channel < 1 || channel > NUM_CH) return;
    int idx = channel - 1;
    s_channels[idx].rssi_floor = rssi_floor;
    s_channels[idx].util_pct   = util_pct;
    s_channels[idx].history[s_channels[idx].hist_idx] = (int8_t)util_pct;
    s_channels[idx].hist_idx = (s_channels[idx].hist_idx + 1) % HISTORY_LEN;
}

static void _start(void) {
    s_sweeping = true;
    pm_c6_cmd_send_raw("{\"cmd\":\"rf_spectrum_start\"}");
}
static void _stop(void) {
    s_sweeping = false;
    pm_c6_cmd_send_raw("{\"cmd\":\"rf_spectrum_stop\"}");
}

static void _render(void) {
    pm_log_d(TAG, "sweep=%d", s_sweeping);
    // TODO_LVGL: bar chart channels 1..14 with util_pct height,
    //            color-coded by RSSI floor; below: waterfall mini-chart.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("RF SPECTRM",
        "RF SPECTRM app — UI ready");
}
static void _init(void) { _build_screen(); }
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
    .id           = "rf_spectrum",
    .display_name = "RF SPECTRM",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_rf_spectrum(void) { return &_APP; }
