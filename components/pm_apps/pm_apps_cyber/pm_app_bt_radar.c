// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_bt_radar.c — Session-aware BT radar
//
//  The defining feature: per-device RSSI history. Every
//  observation goes into a 60-sample ring buffer indexed by
//  MAC. The UI surfaces this as inline sparklines on each
//  row, plus a polar "tracker" plot on the side pane where
//  weaker signals sit farther from center.
//
//  Data layer:
//    - 32 device slots, oldest-recycle when full.
//    - Each slot keeps name, MAC, addr type, manufacturer,
//      60 RSSI samples, last-seen timestamp.
//    - pm_app_bt_radar_on_seen() also forwards each obs into
//      the wardrive logger so the BLE table gets populated
//      even when only BT Radar is foreground.
//
//  Radio plumbing (untouched from earlier work):
//    - pm_peer-based BLE scan ownership. Cardputer or T-Beam
//      can serve as the BLE source via the resource treaty.
//    - On exit the cap is released so other apps can take it.
//
//  Tick rate:
//    - 250 ms render cadence. Sparklines and the radar plot
//      refresh; row labels update in place; no DOM rebuilds.
// ============================================================

#include "pm_app_bt_radar.h"
#include "pm_app_wardrive.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_cardputer_i2c.h"
#include "pm_peer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_BT_RADAR";

// ── Data model ───────────────────────────────────────────
#define MAX_DEVS         32
#define HISTORY_LEN      60   // RSSI ring buffer per device
#define SPARK_POINTS     30   // points shown per row sparkline
#define MAX_VISIBLE_ROWS 24   // UI rows pre-allocated
#define FRESH_MS         2000
#define AGING_MS         10000

typedef struct {
    char     mac[18];
    char     name[32];
    char     addr_type[10];
    char     mfg[24];
    int8_t   rssi_history[HISTORY_LEN];
    int      hist_idx;          // next write position
    int      samples;           // total samples written (capped at HISTORY_LEN for display)
    uint32_t last_seen_ms;
    bool     active;
} radar_dev_t;

static radar_dev_t s_devs[MAX_DEVS];
static bool        s_session_active   = false;
static pm_peer_t*  s_ble_peer         = NULL;
static bool        s_ble_peer_started = false;

// ── Data helpers ─────────────────────────────────────────
static int _find_or_make(const char* mac) {
    for (int i = 0; i < MAX_DEVS; i++) {
        if (s_devs[i].active && strcmp(s_devs[i].mac, mac) == 0) return i;
    }
    int slot = -1;
    uint32_t oldest_ms = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_DEVS; i++) {
        if (!s_devs[i].active) { slot = i; break; }
        if (s_devs[i].last_seen_ms < oldest_ms) {
            oldest_ms = s_devs[i].last_seen_ms;
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    memset(&s_devs[slot], 0, sizeof(radar_dev_t));
    s_devs[slot].active = true;
    return slot;
}

// Read sample at position `back` slots before the latest write.
// back=0 → most recent, back=1 → previous, etc.
static int8_t _sample_at(const radar_dev_t* d, int back) {
    int idx = d->hist_idx - 1 - back;
    while (idx < 0) idx += HISTORY_LEN;
    return d->rssi_history[idx];
}

static int _latest_rssi(const radar_dev_t* d) {
    if (d->samples == 0) return -100;
    return _sample_at(d, 0);
}

static void _stats(const radar_dev_t* d, int* mn, int* mx, int* avg) {
    int n = d->samples;
    if (n > HISTORY_LEN) n = HISTORY_LEN;
    if (n == 0) {
        if (mn) *mn = 0;
        if (mx) *mx = 0;
        if (avg) *avg = 0;
        return;
    }
    int lo = 127, hi = -128, sum = 0;
    for (int i = 0; i < n; i++) {
        int v = _sample_at(d, i);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        sum += v;
    }
    if (mn)  *mn  = lo;
    if (mx)  *mx  = hi;
    if (avg) *avg = sum / n;
}

// ── Public input (called from BLE scan callbacks) ─────────
void pm_app_bt_radar_on_seen(const char* mac, const char* name, int rssi,
                              const char* addr_type, const char* mfg) {
    if (!mac || !s_session_active) return;

    int idx = _find_or_make(mac);
    radar_dev_t* d = &s_devs[idx];

    strncpy(d->mac, mac, sizeof(d->mac) - 1);
    if (name && name[0]) {
        strncpy(d->name, name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';
    }
    if (addr_type && addr_type[0]) {
        strncpy(d->addr_type, addr_type, sizeof(d->addr_type) - 1);
        d->addr_type[sizeof(d->addr_type) - 1] = '\0';
    }
    if (mfg && mfg[0]) {
        strncpy(d->mfg, mfg, sizeof(d->mfg) - 1);
        d->mfg[sizeof(d->mfg) - 1] = '\0';
    }

    if (rssi < -127) rssi = -127;
    if (rssi >  127) rssi =  127;
    d->rssi_history[d->hist_idx] = (int8_t)rssi;
    d->hist_idx = (d->hist_idx + 1) % HISTORY_LEN;
    if (d->samples < HISTORY_LEN * 4) d->samples++;
    d->last_seen_ms = pm_millis();

    // Mirror into wardrive's logger so the session DB tracks every BLE hit.
    pm_app_wardrive_on_ble(mac, name, rssi, addr_type, mfg);
}

void pm_app_bt_radar_start(void) { s_session_active = true; }
void pm_app_bt_radar_stop(void)  { s_session_active = false; }

// ── BLE source negotiation via pm_peer treaty ────────────
static void _stop_ble_source(void) {
    if (!s_ble_peer) return;
    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (s_ble_peer_started &&
        (kind == PM_PEER_KIND_CARDPUTER_I2C || kind == PM_PEER_KIND_TBEAM_S3)) {
        pm_peer_call(s_ble_peer, "ble_scan_stop", NULL);
    }
    pm_peer_release_cap(s_ble_peer, "ble_scan");
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
            pm_peer_release_cap(s_ble_peer, "ble_scan");
            s_ble_peer = NULL;
            return;
        }
        s_ble_peer_started = true;
    } else if (kind == PM_PEER_KIND_TBEAM_S3) {
        int rc = pm_peer_call(s_ble_peer, "ble_scan_start", NULL);
        if (rc != 0) {
            pm_log_w(TAG, "T-Beam BLE start failed rc=%d", rc);
            pm_peer_release_cap(s_ble_peer, "ble_scan");
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

// ── UI helpers ───────────────────────────────────────────

// Color a status dot by how recently the device was heard from.
static lv_color_t _freshness_color(uint32_t now, uint32_t last_seen) {
    uint32_t age = now - last_seen;
    if (age < FRESH_MS) return PM_LAYOUT_COL_OK;
    if (age < AGING_MS) return PM_LAYOUT_COL_GOLD;
    return PM_LAYOUT_COL_DIM;
}

// Map RSSI dBm → bar fill color.
static lv_color_t _rssi_color(int rssi) {
    if (rssi >= -55) return PM_LAYOUT_COL_OK;
    if (rssi >= -67) return PM_LAYOUT_COL_GOLD;
    if (rssi >= -78) return PM_LAYOUT_COL_WARN;
    return PM_LAYOUT_COL_ERR;
}

static int _rssi_pct(int rssi) {
    int v = rssi + 90;
    if (v < 0)  v = 0;
    if (v > 60) v = 60;
    return (v * 100) / 60;
}

// Trim "AA:BB:CC:DD:EE:FF" → "DD:EE:FF" for the compact column.
static const char* _mac_short(const char* mac) {
    int len = (int)strlen(mac);
    return (len >= 8) ? mac + (len - 8) : mac;
}

// Address-type → category colour (PUBLIC vs RANDOM is the most useful
// distinction; everything else collapses to dim).
static lv_color_t _addr_color(const char* at) {
    if (!at || !at[0]) return PM_LAYOUT_COL_DIM;
    if (strstr(at, "pub") || strstr(at, "PUB")) return PM_LAYOUT_COL_ACCENT;
    if (strstr(at, "rnd") || strstr(at, "RND")
        || strstr(at, "ran") || strstr(at, "RAN")) return PM_LAYOUT_COL_PURPLE;
    return PM_LAYOUT_COL_DIM;
}

// ── UI state ─────────────────────────────────────────────
typedef struct {
    lv_obj_t* row;
    lv_obj_t* dot;
    lv_obj_t* spark;
    lv_obj_t* rssi_bar;
    lv_obj_t* rssi_lbl;
    lv_obj_t* mac_lbl;
    lv_obj_t* name_lbl;
    lv_obj_t* addr_lbl;
    // Owned per-row sparkline point buffer.
    lv_point_precise_t spark_pts[SPARK_POINTS];
} dev_row_ui_t;

static dev_row_ui_t s_rows[MAX_VISIBLE_ROWS];
static int          s_rows_created = 0;

// Header chips
static lv_obj_t* s_chip_status = NULL;
static lv_obj_t* s_chip_source = NULL;

// Stats labels
static lv_obj_t* s_stat_devices = NULL;
static lv_obj_t* s_stat_fresh   = NULL;
static lv_obj_t* s_stat_named   = NULL;
static lv_obj_t* s_stat_best    = NULL;

// Right pane: polar "tracker" + detail block
static lv_obj_t* s_radar_plate     = NULL;   // square frame for radar
static lv_obj_t* s_radar_dots[MAX_DEVS] = {0};
static lv_obj_t* s_detail_name_lbl = NULL;
static lv_obj_t* s_detail_mac_lbl  = NULL;
static lv_obj_t* s_detail_mfg_lbl  = NULL;
static lv_obj_t* s_detail_stats_lbl = NULL;
static lv_obj_t* s_detail_spark    = NULL;   // big sparkline for strongest device
static lv_point_precise_t s_detail_spark_pts[HISTORY_LEN];

// Scrollable list container
static lv_obj_t* s_ap_list = NULL;

// Cached radar geometry — recomputed when the pane is first sized.
static int s_radar_cx       = 80;
static int s_radar_cy       = 80;
static int s_radar_max_r    = 70;
static bool s_radar_geom_done = false;

static lv_obj_t* s_screen = NULL;
static bool      s_built  = false;

// ── Row builder ──────────────────────────────────────────
//
// 36 px tall row, horizontal flex:
//   dot | mini-spark | RSSI bar+dBm | MAC tail | name | addr badge
//
static void _build_row(int idx, lv_obj_t* parent) {
    dev_row_ui_t* r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 36);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 8, 0);
    lv_obj_set_style_pad_ver(r->row, 4, 0);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    // Status dot
    r->dot = lv_obj_create(r->row);
    lv_obj_remove_style_all(r->dot);
    lv_obj_set_size(r->dot, 10, 10);
    lv_obj_set_style_bg_color(r->dot, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);

    // Mini sparkline — fixed width container with an lv_line inside
    lv_obj_t* spark_box = lv_obj_create(r->row);
    lv_obj_remove_style_all(spark_box);
    lv_obj_set_size(spark_box, 96, 24);
    lv_obj_set_style_bg_color(spark_box, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(spark_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(spark_box, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(spark_box, 1, 0);
    lv_obj_set_style_radius(spark_box, 2, 0);
    lv_obj_clear_flag(spark_box, LV_OBJ_FLAG_SCROLLABLE);
    r->spark = lv_line_create(spark_box);
    lv_obj_set_style_line_color(r->spark, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_set_style_line_width(r->spark, 1, 0);
    lv_obj_set_style_line_opa(r->spark, LV_OPA_COVER, 0);
    lv_obj_align(r->spark, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(r->spark, 94, 22);

    // RSSI bar in a small column with the dBm number on top
    lv_obj_t* rssi_box = lv_obj_create(r->row);
    lv_obj_remove_style_all(rssi_box);
    lv_obj_set_size(rssi_box, 110, 28);
    lv_obj_set_style_bg_opa(rssi_box, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(rssi_box, LV_OBJ_FLAG_SCROLLABLE);
    r->rssi_bar = lv_bar_create(rssi_box);
    lv_obj_remove_style_all(r->rssi_bar);
    lv_obj_set_size(r->rssi_bar, 110, 12);
    lv_obj_align(r->rssi_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(r->rssi_bar, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(r->rssi_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r->rssi_bar, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(r->rssi_bar, 1, 0);
    lv_obj_set_style_radius(r->rssi_bar, 2, 0);
    lv_obj_set_style_bg_color(r->rssi_bar, PM_LAYOUT_COL_OK, LV_PART_INDICATOR);
    lv_obj_set_style_radius(r->rssi_bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(r->rssi_bar, 0, 100);
    lv_bar_set_value(r->rssi_bar, 0, LV_ANIM_OFF);
    r->rssi_lbl = lv_label_create(rssi_box);
    lv_label_set_text(r->rssi_lbl, "— dBm");
    lv_obj_set_style_text_font(r->rssi_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->rssi_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_align(r->rssi_lbl, LV_ALIGN_TOP_LEFT, 2, 0);

    // MAC (last two octets)
    r->mac_lbl = lv_label_create(r->row);
    lv_label_set_text(r->mac_lbl, "—:—:—");
    lv_obj_set_style_text_font(r->mac_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->mac_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->mac_lbl, 78);

    // Name — flex-grow
    r->name_lbl = lv_label_create(r->row);
    lv_label_set_text(r->name_lbl, "(unnamed)");
    lv_label_set_long_mode(r->name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(r->name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->name_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_flex_grow(r->name_lbl, 1);

    // Address-type chip
    lv_obj_t* addr_chip = lv_obj_create(r->row);
    lv_obj_remove_style_all(addr_chip);
    lv_obj_set_size(addr_chip, 60, 18);
    lv_obj_set_style_bg_color(addr_chip, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_bg_opa(addr_chip, 30, 0);
    lv_obj_set_style_border_color(addr_chip, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_border_width(addr_chip, 1, 0);
    lv_obj_set_style_radius(addr_chip, 3, 0);
    lv_obj_clear_flag(addr_chip, LV_OBJ_FLAG_SCROLLABLE);
    r->addr_lbl = lv_label_create(addr_chip);
    lv_label_set_text(r->addr_lbl, "—");
    lv_obj_set_style_text_font(r->addr_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->addr_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_center(r->addr_lbl);
}

// ── Right pane (radar plate + detail) ─────────────────────
static void _build_radar_plate(lv_obj_t* pane) {
    // The radar is a square plate with concentric rings. We size it
    // square at runtime based on the pane width.
    s_radar_plate = lv_obj_create(pane);
    lv_obj_remove_style_all(s_radar_plate);
    lv_obj_set_width(s_radar_plate, LV_PCT(100));
    // Height set after layout in _render.
    lv_obj_set_height(s_radar_plate, 200);
    lv_obj_set_style_bg_color(s_radar_plate, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_radar_plate, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_radar_plate, LV_OBJ_FLAG_SCROLLABLE);

    // Concentric range rings (3 of them)
    for (int i = 1; i <= 3; i++) {
        lv_obj_t* ring = lv_obj_create(s_radar_plate);
        lv_obj_remove_style_all(ring);
        // Placeholder size; recomputed when geometry is known.
        lv_obj_set_size(ring, 40 * i, 40 * i);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(ring, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_opa(ring, 120, 0);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        // Tag the ring index in user data so _render can find it.
        lv_obj_set_user_data(ring, (void*)(intptr_t)i);
    }

    // Crosshair (vertical + horizontal subtle lines via thin lv_obj)
    lv_obj_t* hx = lv_obj_create(s_radar_plate);
    lv_obj_remove_style_all(hx);
    lv_obj_set_size(hx, LV_PCT(90), 1);
    lv_obj_set_style_bg_color(hx, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_bg_opa(hx, 80, 0);
    lv_obj_align(hx, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* vx = lv_obj_create(s_radar_plate);
    lv_obj_remove_style_all(vx);
    lv_obj_set_size(vx, 1, LV_PCT(90));
    lv_obj_set_style_bg_color(vx, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_bg_opa(vx, 80, 0);
    lv_obj_align(vx, LV_ALIGN_CENTER, 0, 0);

    // Pre-create all possible device dots (hidden until used).
    for (int i = 0; i < MAX_DEVS; i++) {
        lv_obj_t* d = lv_obj_create(s_radar_plate);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, PM_LAYOUT_COL_ACCENT, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(d, PM_LAYOUT_COL_FG_BR, 0);
        lv_obj_set_style_border_width(d, 1, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_radar_dots[i] = d;
    }
}

static void _build_detail_panel(lv_obj_t* pane) {
    // Detail block under the radar — shows the strongest device's
    // info and a full-width sparkline of its RSSI history.
    lv_obj_t* hdr = lv_label_create(pane);
    lv_label_set_text(hdr, "STRONGEST DEVICE");
    lv_obj_set_style_text_font(hdr, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(hdr, 1, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_set_style_pad_top(hdr, 6, 0);

    s_detail_name_lbl = lv_label_create(pane);
    lv_label_set_text(s_detail_name_lbl, "(none)");
    lv_label_set_long_mode(s_detail_name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_detail_name_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_detail_name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_detail_name_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_style_pad_left(s_detail_name_lbl, 10, 0);

    s_detail_mac_lbl = lv_label_create(pane);
    lv_label_set_text(s_detail_mac_lbl, "—");
    lv_obj_set_style_text_font(s_detail_mac_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_mac_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_pad_left(s_detail_mac_lbl, 10, 0);

    s_detail_mfg_lbl = lv_label_create(pane);
    lv_label_set_text(s_detail_mfg_lbl, "");
    lv_label_set_long_mode(s_detail_mfg_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_detail_mfg_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_detail_mfg_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_mfg_lbl, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_set_style_pad_left(s_detail_mfg_lbl, 10, 0);

    // Big sparkline — bordered box with lv_line inside.
    lv_obj_t* sp_box = lv_obj_create(pane);
    lv_obj_remove_style_all(sp_box);
    lv_obj_set_width(sp_box, LV_PCT(95));
    lv_obj_set_height(sp_box, 60);
    lv_obj_set_style_bg_color(sp_box, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(sp_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sp_box, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(sp_box, 1, 0);
    lv_obj_set_style_radius(sp_box, 2, 0);
    lv_obj_set_style_margin_left(sp_box, 10, 0);
    lv_obj_set_style_margin_top(sp_box, 4, 0);
    lv_obj_clear_flag(sp_box, LV_OBJ_FLAG_SCROLLABLE);
    s_detail_spark = lv_line_create(sp_box);
    lv_obj_set_style_line_color(s_detail_spark, PM_LAYOUT_COL_OK, 0);
    lv_obj_set_style_line_width(s_detail_spark, 2, 0);
    lv_obj_align(s_detail_spark, LV_ALIGN_TOP_LEFT, 0, 0);

    s_detail_stats_lbl = lv_label_create(pane);
    lv_label_set_text(s_detail_stats_lbl, "min — / avg — / max —");
    lv_obj_set_style_text_font(s_detail_stats_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_stats_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_pad_left(s_detail_stats_lbl, 10, 0);
    lv_obj_set_style_pad_top(s_detail_stats_lbl, 4, 0);
}

// ── Action callbacks ─────────────────────────────────────
static void _act_clear(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "clear");
    memset(s_devs, 0, sizeof(s_devs));
}

static void _act_export(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "export (TODO — wardrive CSV/SQLite path)");
}

// ── Screen build (lazy) ──────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "BT RADAR");

    s_chip_status = pm_app_layout_chip(&L, "STANDBY",  PM_LAYOUT_COL_DIM);
    s_chip_source = pm_app_layout_chip(&L, "NO RADIO", PM_LAYOUT_COL_WARN);

    pm_app_layout_stats_row(&L, 4);
    s_stat_devices = pm_app_layout_stat(&L, "DEVICES",   "0");
    s_stat_fresh   = pm_app_layout_stat(&L, "FRESH",     "0");
    s_stat_named   = pm_app_layout_stat(&L, "NAMED",     "0");
    s_stat_best    = pm_app_layout_stat(&L, "STRONGEST", "—");

    pm_app_layout_content(&L);

    // Left pane: scrollable device list
    lv_obj_t* list_pane = pm_app_layout_pane(&L, 0, "DETECTED DEVICES");
    s_ap_list = lv_obj_create(list_pane);
    lv_obj_remove_style_all(s_ap_list);
    lv_obj_set_width(s_ap_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ap_list, 1);
    lv_obj_set_style_bg_opa(s_ap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_ap_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_ap_list, 1, 0);
    lv_obj_set_style_pad_all(s_ap_list, 0, 0);
    lv_obj_add_flag(s_ap_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ap_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ap_list, LV_SCROLLBAR_MODE_AUTO);

    // Right pane: radar + detail
#if PM_BOARD_LCD_H_RES <= 800
    int side_w = 240;
#else
    int side_w = 340;
#endif
    lv_obj_t* side_pane = pm_app_layout_pane(&L, side_w, "TRACKER");
    _build_radar_plate(side_pane);
    _build_detail_panel(side_pane);

    pm_app_layout_action(&L, "CLEAR",  PM_LAYOUT_COL_WARN,   _act_clear);
    pm_app_layout_action(&L, "EXPORT", PM_LAYOUT_COL_ACCENT, _act_export);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Sparkline plotting ───────────────────────────────────
//
// Map the last SPARK_POINTS RSSI samples of `d` to pixel coords
// inside a (w_px × h_px) area, writing into `dst[]`. We always
// produce exactly SPARK_POINTS points — for new devices with
// fewer samples, we replicate the latest value so the line stays
// flat rather than collapsing into a dot.
static void _build_spark_pts(const radar_dev_t* d,
                              lv_point_precise_t* dst,
                              int w_px, int h_px, int point_count) {
    int n = d->samples;
    if (n > HISTORY_LEN) n = HISTORY_LEN;

    for (int i = 0; i < point_count; i++) {
        // sample `back` runs from oldest to newest as i grows.
        int back = (point_count - 1) - i;
        int v;
        if (n == 0) {
            v = -90;
        } else if (back >= n) {
            v = _sample_at(d, n - 1);
        } else {
            v = _sample_at(d, back);
        }
        int pct = _rssi_pct(v);
        int x = (i * (w_px - 1)) / (point_count - 1);
        int y = (h_px - 1) - ((pct * (h_px - 1)) / 100);
        dst[i].x = x;
        dst[i].y = y;
    }
}

// ── Render ───────────────────────────────────────────────
//
// Sort devices by current RSSI desc into a side index; bind UI
// rows to that index; update sparklines, stats, source chip,
// radar dots, and the strongest-device detail block.
static void _render(void) {
    if (!s_built) return;

    uint32_t now = pm_millis();

    // Index of active devices sorted by latest RSSI desc.
    int sorted[MAX_DEVS];
    int count = 0;
    for (int i = 0; i < MAX_DEVS; i++) {
        if (s_devs[i].active) sorted[count++] = i;
    }
    // Insertion sort — small N (≤32), keeps things stable.
    for (int i = 1; i < count; i++) {
        int key = sorted[i];
        int key_rssi = _latest_rssi(&s_devs[key]);
        int j = i - 1;
        while (j >= 0 && _latest_rssi(&s_devs[sorted[j]]) < key_rssi) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // Header source chip.
    if (s_chip_source) {
        const char* src = "NO RADIO";
        lv_color_t  col = PM_LAYOUT_COL_WARN;
        if (s_ble_peer) {
            pm_peer_kind_t k = pm_peer_kind(s_ble_peer);
            if (k == PM_PEER_KIND_CARDPUTER_I2C) { src = "CARDPUTER"; col = PM_LAYOUT_COL_OK; }
            else if (k == PM_PEER_KIND_TBEAM_S3) { src = "T-BEAM";    col = PM_LAYOUT_COL_OK; }
            else                                 { src = "PEER";      col = PM_LAYOUT_COL_ACCENT; }
        }
        lv_label_set_text(s_chip_source, src);
        lv_obj_set_style_text_color(s_chip_source, col, 0);
        lv_obj_t* chip = lv_obj_get_parent(s_chip_source);
        if (chip) {
            lv_obj_set_style_border_color(chip, col, 0);
            lv_obj_set_style_bg_color(chip, col, 0);
        }
    }
    if (s_chip_status) {
        const char* st  = s_session_active ? "SCANNING" : "STANDBY";
        lv_color_t  col = s_session_active ? PM_LAYOUT_COL_OK : PM_LAYOUT_COL_DIM;
        lv_label_set_text(s_chip_status, st);
        lv_obj_set_style_text_color(s_chip_status, col, 0);
        lv_obj_t* chip = lv_obj_get_parent(s_chip_status);
        if (chip) {
            lv_obj_set_style_border_color(chip, col, 0);
            lv_obj_set_style_bg_color(chip, col, 0);
        }
    }

    // Stats tally.
    int fresh = 0, named = 0;
    int best  = 0;
    bool best_set = false;
    for (int i = 0; i < count; i++) {
        const radar_dev_t* d = &s_devs[sorted[i]];
        if (now - d->last_seen_ms < FRESH_MS) fresh++;
        if (d->name[0]) named++;
        int r = _latest_rssi(d);
        if (!best_set || r > best) { best = r; best_set = true; }
    }
    if (s_stat_devices) { char b[8]; snprintf(b, sizeof(b), "%d", count);  lv_label_set_text(s_stat_devices, b); }
    if (s_stat_fresh)   { char b[8]; snprintf(b, sizeof(b), "%d", fresh);  lv_label_set_text(s_stat_fresh, b); }
    if (s_stat_named)   { char b[8]; snprintf(b, sizeof(b), "%d", named);  lv_label_set_text(s_stat_named, b); }
    if (s_stat_best) {
        char b[12];
        if (best_set) snprintf(b, sizeof(b), "%d", best);
        else          snprintf(b, sizeof(b), "—");
        lv_label_set_text(s_stat_best, b);
    }

    // Radar geometry — compute once we have a real layout.
    if (s_radar_plate && !s_radar_geom_done) {
        lv_obj_update_layout(s_radar_plate);
        int pw = lv_obj_get_width(s_radar_plate);
        int ph = lv_obj_get_height(s_radar_plate);
        if (pw > 20 && ph > 20) {
            int side = pw < ph ? pw : ph;
            lv_obj_set_height(s_radar_plate, side);
            s_radar_cx     = pw / 2;
            s_radar_cy     = side / 2;
            s_radar_max_r  = (side / 2) - 8;

            // Re-size rings now that we have real geometry.
            int child_count = lv_obj_get_child_count(s_radar_plate);
            for (int i = 0; i < child_count; i++) {
                lv_obj_t* c = lv_obj_get_child(s_radar_plate, i);
                intptr_t  ring_idx = (intptr_t)lv_obj_get_user_data(c);
                if (ring_idx >= 1 && ring_idx <= 3) {
                    int rsz = (s_radar_max_r * ring_idx) / 3;
                    lv_obj_set_size(c, rsz * 2, rsz * 2);
                    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
                }
            }
            s_radar_geom_done = true;
        }
    }

    // Radar dots — place active devices by angle (slot-based, 360°/MAX_DEVS)
    // and radius (weaker = farther from center).
    for (int i = 0; i < MAX_DEVS; i++) {
        if (!s_radar_dots[i]) continue;
        if (!s_devs[i].active) {
            lv_obj_add_flag(s_radar_dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int r        = _latest_rssi(&s_devs[i]);
        int pct      = _rssi_pct(r);
        int dist     = s_radar_max_r - ((pct * s_radar_max_r) / 100);
        // Hash a stable bearing into the device's MAC so positions
        // don't shuffle each render. Sum of MAC bytes mod 360.
        unsigned hash = 0;
        for (int k = 0; s_devs[i].mac[k]; k++) hash = hash * 31u + (unsigned char)s_devs[i].mac[k];
        int ang_idx = hash % 360;
        // Quick sin/cos via a 16-entry lookup. Saves us a math.h pull.
        static const int sin_table[16] = {
            0, 38, 70, 92, 100, 92, 70, 38,
            0, -38, -70, -92, -100, -92, -70, -38
        };
        int qa = (ang_idx * 16) / 360;
        int qb = (qa + 4) & 0x0F;   // +90° for cos
        int sx = sin_table[qb];      // cos(a)
        int sy = sin_table[qa];      // sin(a)
        int dx = (dist * sx) / 100;
        int dy = (dist * sy) / 100;

        lv_color_t col = _rssi_color(r);
        lv_obj_set_style_bg_color(s_radar_dots[i], col, 0);
        lv_obj_align(s_radar_dots[i], LV_ALIGN_CENTER, dx, dy);
        lv_obj_clear_flag(s_radar_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    // List rows — bind up to MAX_VISIBLE_ROWS sorted entries.
    int visible = 0;
    for (int i = 0; i < count && i < MAX_VISIBLE_ROWS; i++) {
        const radar_dev_t* d = &s_devs[sorted[i]];

        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_ap_list);
            s_rows_created++;
        }
        dev_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);

        // Status dot
        lv_obj_set_style_bg_color(r->dot,
            _freshness_color(now, d->last_seen_ms), 0);

        // Sparkline
        _build_spark_pts(d, r->spark_pts, 94, 22, SPARK_POINTS);
        lv_line_set_points(r->spark, r->spark_pts, SPARK_POINTS);
        int latest = _latest_rssi(d);
        lv_obj_set_style_line_color(r->spark, _rssi_color(latest), 0);

        // RSSI bar + label
        lv_bar_set_value(r->rssi_bar, _rssi_pct(latest), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(r->rssi_bar, _rssi_color(latest),
                                   LV_PART_INDICATOR);
        char rb[16];
        snprintf(rb, sizeof(rb), "%d dBm", latest);
        lv_label_set_text(r->rssi_lbl, rb);

        // MAC tail + name
        lv_label_set_text(r->mac_lbl, _mac_short(d->mac));
        lv_label_set_text(r->name_lbl, d->name[0] ? d->name : "(unnamed)");
        lv_obj_set_style_text_color(r->name_lbl,
            d->name[0] ? PM_LAYOUT_COL_FG_BR : PM_LAYOUT_COL_DIM, 0);

        // Address-type chip
        lv_color_t ac = _addr_color(d->addr_type);
        const char* atxt = d->addr_type[0] ? d->addr_type : "?";
        lv_label_set_text(r->addr_lbl, atxt);
        lv_obj_set_style_text_color(r->addr_lbl, ac, 0);
        lv_obj_t* addr_chip = lv_obj_get_parent(r->addr_lbl);
        if (addr_chip) {
            lv_obj_set_style_border_color(addr_chip, ac, 0);
            lv_obj_set_style_bg_color(addr_chip, ac, 0);
        }

        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    // Strongest-device detail block.
    if (count > 0 && s_detail_spark) {
        const radar_dev_t* d = &s_devs[sorted[0]];
        lv_label_set_text(s_detail_name_lbl,
                           d->name[0] ? d->name : "(unnamed)");
        lv_label_set_text(s_detail_mac_lbl, d->mac);
        lv_label_set_text(s_detail_mfg_lbl,
                           d->mfg[0] ? d->mfg : "");

        int detail_w =
#if PM_BOARD_LCD_H_RES <= 800
            210;
#else
            300;
#endif
        _build_spark_pts(d, s_detail_spark_pts, detail_w, 58, HISTORY_LEN);
        lv_line_set_points(s_detail_spark, s_detail_spark_pts, HISTORY_LEN);

        int mn, mx, av;
        _stats(d, &mn, &mx, &av);
        char sb[64];
        snprintf(sb, sizeof(sb), "min %d  /  avg %d  /  max %d  /  %d samples",
                  mn, av, mx, d->samples > HISTORY_LEN ? HISTORY_LEN : d->samples);
        lv_label_set_text(s_detail_stats_lbl, sb);
    } else if (s_detail_name_lbl) {
        lv_label_set_text(s_detail_name_lbl, "(none)");
        lv_label_set_text(s_detail_mac_lbl,  "—");
        lv_label_set_text(s_detail_mfg_lbl,  "");
        lv_label_set_text(s_detail_stats_lbl, "no devices yet");
    }
}

// ── App lifecycle ────────────────────────────────────────
static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    s_session_active = true;
    _start_ble_source();
    _render();
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    s_session_active = false;
    _stop_ble_source();
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    s_last_render = now;
    _poll_external_ble();
    _render();
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
