// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_gps.c — Live GPS readout
//
//  Renders pm_gps_state. P4 has way more screen than the S3
//  T-Deck — the layout is:
//    Header: status + GPS fix indicator
//    Big lat/lng readout
//    Altitude / speed / sat count side panel
//    "Last update Ns ago" row
//    [COPY COORDS] button (writes lat,lng to NoSQL clipboard)
// ============================================================

#include "pm_app_gps.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "pm_ui.h"
#include "pm_gps_state.h"
#include "pm_gps_uart.h"
#include "pm_board.h"
#include "pm_nosql.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_GPS";

// LVGL
static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_lbl_status  = NULL;
static lv_obj_t* s_lbl_lat     = NULL;
static lv_obj_t* s_lbl_lng     = NULL;
static lv_obj_t* s_lbl_alt     = NULL;
static lv_obj_t* s_lbl_speed   = NULL;
static lv_obj_t* s_lbl_sats    = NULL;
static lv_obj_t* s_lbl_age     = NULL;
static lv_obj_t* s_lbl_uart    = NULL;
static lv_obj_t* s_lbl_nmea    = NULL;
static lv_obj_t* s_lbl_fixes   = NULL;

static uint32_t s_last_render_ms = 0;

static lv_obj_t* _metric_card(lv_obj_t* parent, const char* key,
                              const char* initial) {
    lv_obj_t* card = pm_ui_card(parent);
    lv_obj_set_size(card, 310, 72);
    return pm_ui_kv_row(card, key, initial);
}

static void _render(void) {
    pm_gps_t g;
    pm_gps_state_get(&g);
#if PM_BOARD_LOCAL_GPS_UART
    pm_gps_uart_stats_t st;
    pm_gps_uart_stats(&st);
#endif

    char buf[96];
    if (g.last_update_ms == 0) {
#if PM_BOARD_LOCAL_GPS_UART
        if (st.bytes_rx == 0) {
            snprintf(buf, sizeof(buf), "STATUS: waiting for NMEA on UART4 RX IO52");
        } else if (st.sentences_seen == 0) {
            snprintf(buf, sizeof(buf), "STATUS: UART active, no valid NMEA checksum yet");
        } else {
            snprintf(buf, sizeof(buf), "STATUS: NMEA active, waiting for GPS state");
        }
#else
        snprintf(buf, sizeof(buf), "STATUS: waiting for Cardputer UART1 GPS");
#endif
    } else if (g.valid) {
        snprintf(buf, sizeof(buf), "STATUS: FIX  |  sats %d", g.sats);
    } else {
        snprintf(buf, sizeof(buf), "STATUS: NO FIX  |  sats %d", g.sats);
    }
    if (s_lbl_status) lv_label_set_text(s_lbl_status, buf);

    if (g.last_update_ms != 0 && (g.valid || g.lat != 0.0 || g.lng != 0.0)) {
        snprintf(buf, sizeof(buf), "%+.6f", g.lat);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (s_lbl_lat) lv_label_set_text(s_lbl_lat, buf);
    if (g.last_update_ms != 0 && (g.valid || g.lat != 0.0 || g.lng != 0.0)) {
        snprintf(buf, sizeof(buf), "%+.6f", g.lng);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (s_lbl_lng) lv_label_set_text(s_lbl_lng, buf);

    snprintf(buf, sizeof(buf), "%.1f m", g.alt_m);
    if (s_lbl_alt) lv_label_set_text(s_lbl_alt, buf);
    snprintf(buf, sizeof(buf), "%.1f m/s", g.speed_mps);
    if (s_lbl_speed) lv_label_set_text(s_lbl_speed, buf);
    snprintf(buf, sizeof(buf), "%d", g.sats);
    if (s_lbl_sats) lv_label_set_text(s_lbl_sats, buf);

    if (g.last_update_ms == 0) {
        snprintf(buf, sizeof(buf), "no GPS state updates yet");
    } else {
        uint32_t age = pm_millis() - g.last_update_ms;
        snprintf(buf, sizeof(buf), "%u ms ago", (unsigned)age);
    }
    if (s_lbl_age) lv_label_set_text(s_lbl_age, buf);

#if PM_BOARD_LOCAL_GPS_UART
    snprintf(buf, sizeof(buf), "%u bytes on UART1",
             (unsigned)st.bytes_rx);
    if (s_lbl_uart) lv_label_set_text(s_lbl_uart, buf);
    snprintf(buf, sizeof(buf), "%u good / %u bad sentences",
             (unsigned)st.sentences_seen, (unsigned)st.sentences_bad);
    if (s_lbl_nmea) lv_label_set_text(s_lbl_nmea, buf);
    snprintf(buf, sizeof(buf), "%u valid / %u no-fix RMC",
             (unsigned)st.fixes_valid, (unsigned)st.fixes_invalid);
    if (s_lbl_fixes) lv_label_set_text(s_lbl_fixes, buf);
#else
    if (s_lbl_uart) lv_label_set_text(s_lbl_uart, "Cardputer ADV UART1 header");
    if (s_lbl_nmea) lv_label_set_text(s_lbl_nmea, "GPS updates via pm_gps_state");
    if (s_lbl_fixes) lv_label_set_text(s_lbl_fixes, "LoRa/GPS/radio companion source");
#endif
}

static void _action_copy_coords(void) {
    pm_gps_t g;
    pm_gps_state_get(&g);
    if (!g.valid) return;
    char line[64];
    int n = snprintf(line, sizeof(line), "%.6f,%.6f", g.lat, g.lng);
    pm_nosql_write("clipboard", "coords", line, (size_t)n);
    pm_log_i(TAG, "copied: %s", line);
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "GPS", NULL, NULL);

    lv_obj_t* status = pm_ui_card(s_screen);
    lv_obj_set_width(status, LV_PCT(100));
    s_lbl_status = lv_label_create(status);
    lv_label_set_text(s_lbl_status, "STATUS: starting");
    lv_obj_set_style_text_color(s_lbl_status, PM_C_FG, 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_20, 0);

    lv_obj_t* metrics = lv_obj_create(s_screen);
    lv_obj_remove_style_all(metrics);
    lv_obj_set_width(metrics, LV_PCT(100));
    lv_obj_set_flex_grow(metrics, 1);
    lv_obj_set_layout(metrics, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(metrics, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(metrics, 10, 0);
    lv_obj_set_style_pad_gap(metrics, 10, 0);

    s_lbl_lat = _metric_card(metrics, "LAT", "--");
    s_lbl_lng = _metric_card(metrics, "LNG", "--");
    s_lbl_sats = _metric_card(metrics, "SATS", "0");
    s_lbl_alt = _metric_card(metrics, "ALT", "0.0 m");
    s_lbl_speed = _metric_card(metrics, "SPEED", "0.0 m/s");
    s_lbl_age = _metric_card(metrics, "AGE", "no GPS state updates yet");

    lv_obj_t* diag = pm_ui_card(s_screen);
    lv_obj_set_width(diag, LV_PCT(100));
    s_lbl_uart = pm_ui_kv_row(diag, "SOURCE",
#if PM_BOARD_LOCAL_GPS_UART
                              "UART4 IO52"
#else
                              "Cardputer ADV UART1"
#endif
    );
    s_lbl_nmea = pm_ui_kv_row(diag, "STREAM",
#if PM_BOARD_LOCAL_GPS_UART
                              "0 good / 0 bad sentences"
#else
                              "waiting for bridge update"
#endif
    );
    s_lbl_fixes = pm_ui_kv_row(diag, "FIXES",
#if PM_BOARD_LOCAL_GPS_UART
                               "0 valid / 0 no-fix RMC"
#else
                               "Cardputer GPS"
#endif
    );
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    s_last_render_ms = 0;
    _render();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 250) return;
    s_last_render_ms = now;
    if (!lvgl_port_lock(0)) return;
    _render();
    lvgl_port_unlock();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "gps",
    .display_name = "GPS",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_gps(void) {
    (void)_action_copy_coords;
    return &_APP;
}
