// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_tracker_scan.c — AirTag / Tile / SmartTag detector
//
//  Detection strategy on P4:
//
//  The S3 build (which has direct NimBLE access) reads the raw
//  Manufacturer Specific Data and matches Company IDs (Apple
//  0x004C with type 0x12, Tile 0x00A8, Samsung 0x0075). On P4
//  we get BLE events through the Cardputer/T-Beam peer, which
//  forwards a pre-decoded "mfg" string and "name". So we match
//  on textual hints:
//
//    AIRTAG     — name "AirTag", mfg "Apple" with name "FindMy"
//    TILE       — name starts "Tile", mfg "Tile"
//    SMARTTAG   — name "Galaxy SmartTag", mfg "Samsung"
//    SUSPECTED  — no name, mfg "Apple"/"Tile"/"Samsung", short
//                 random MAC (the classic privacy-rotating tag
//                 signature). We flag these for the user to
//                 watch with extra care.
//
//  This isn't perfect detection — it's heuristic. We err on
//  the side of false positives so the user actually sees
//  potentially-following devices.
//
//  Persistence is the actual stalking signal: a tracker that's
//  been near you for >5 minutes across location changes is the
//  thing that matters. We tag each detected device with
//  first_seen and current age; the UI surfaces this prominently.
// ============================================================

#include "pm_app_tracker_scan.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_cardputer_i2c.h"
#include "pm_peer.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static const char* TAG = "PM_TRACKER";

// ── Data model ───────────────────────────────────────────
#define MAX_TRACKERS  24
#define ALERT_AGE_MS  (5 * 60 * 1000)   // 5 minutes near = alert
#define STALE_MS      (60 * 1000)

typedef enum {
    TS_KIND_UNKNOWN = 0,
    TS_KIND_AIRTAG,
    TS_KIND_TILE,
    TS_KIND_SMARTTAG,
    TS_KIND_SUSPECTED_APPLE,
    TS_KIND_SUSPECTED_TILE,
    TS_KIND_SUSPECTED_SAMSUNG,
    TS_KIND_COUNT
} ts_kind_t;

typedef struct {
    char     mac[18];
    char     name[32];
    char     mfg[24];
    int      latest_rssi;
    int      best_rssi;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    int      hit_count;
    ts_kind_t kind;
    bool     active;
} ts_dev_t;

static ts_dev_t   s_devs[MAX_TRACKERS];
static bool       s_session_active = false;
static pm_peer_t* s_ble_peer = NULL;
static bool       s_ble_started = false;

// ── Heuristics ───────────────────────────────────────────

// Case-insensitive substring presence.
static bool _has_ci(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl == 0 || nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        if (strncasecmp(hay + i, needle, nl) == 0) return true;
    }
    return false;
}

static ts_kind_t _classify(const char* name, const char* mfg) {
    if (!name) name = "";
    if (!mfg)  mfg  = "";

    // Named matches first — most reliable.
    if (_has_ci(name, "airtag"))          return TS_KIND_AIRTAG;
    if (_has_ci(name, "find my"))         return TS_KIND_AIRTAG;
    if (_has_ci(name, "smarttag"))        return TS_KIND_SMARTTAG;
    if (_has_ci(name, "galaxy smarttag")) return TS_KIND_SMARTTAG;
    if (_has_ci(name, "tile_"))           return TS_KIND_TILE;
    if (name[0] && _has_ci(mfg, "tile"))  return TS_KIND_TILE;

    // Manufacturer-only hits (unnamed tracker — privacy rotation).
    // These are the dangerous ones; they're harder to identify but
    // the most likely to be a tracker silently following you.
    bool is_apple   = _has_ci(mfg, "apple");
    bool is_tile    = _has_ci(mfg, "tile");
    bool is_samsung = _has_ci(mfg, "samsung") || _has_ci(mfg, "smart");

    if (!name[0]) {
        if (is_apple)   return TS_KIND_SUSPECTED_APPLE;
        if (is_tile)    return TS_KIND_SUSPECTED_TILE;
        if (is_samsung) return TS_KIND_SUSPECTED_SAMSUNG;
    }
    return TS_KIND_UNKNOWN;
}

// Find existing slot for a MAC, or claim/recycle one.
static int _slot_for(const char* mac) {
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (s_devs[i].active && strcmp(s_devs[i].mac, mac) == 0) return i;
    }
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (!s_devs[i].active) { slot = i; break; }
        if (s_devs[i].last_seen_ms < oldest) {
            oldest = s_devs[i].last_seen_ms;
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    memset(&s_devs[slot], 0, sizeof(ts_dev_t));
    s_devs[slot].active = true;
    return slot;
}

// Fold one BLE observation into the tracker table. Non-tracker
// devices are silently ignored.
static void _on_ble(const char* mac, const char* name, int rssi,
                     const char* mfg) {
    if (!s_session_active) return;
    if (!mac || !mac[0])   return;

    ts_kind_t k = _classify(name, mfg);
    if (k == TS_KIND_UNKNOWN) return;

    int idx = _slot_for(mac);
    ts_dev_t* d = &s_devs[idx];

    uint32_t now = pm_millis();
    if (d->first_seen_ms == 0) d->first_seen_ms = now;
    d->last_seen_ms = now;
    d->latest_rssi  = rssi;
    if (rssi > d->best_rssi || d->hit_count == 0) d->best_rssi = rssi;
    d->hit_count++;
    d->kind = k;
    strncpy(d->mac, mac, sizeof(d->mac) - 1);
    if (name && name[0]) {
        strncpy(d->name, name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';
    }
    if (mfg && mfg[0]) {
        strncpy(d->mfg, mfg, sizeof(d->mfg) - 1);
        d->mfg[sizeof(d->mfg) - 1] = '\0';
    }
}

// ── BLE source via pm_peer ───────────────────────────────
static void _stop_ble(void) {
    if (!s_ble_peer) return;
    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (s_ble_started &&
        (kind == PM_PEER_KIND_CARDPUTER_I2C || kind == PM_PEER_KIND_TBEAM_S3)) {
        pm_peer_call(s_ble_peer, "ble_scan_stop", NULL);
    }
    pm_peer_release_cap(s_ble_peer, "ble_scan");
    pm_log_i(TAG, "BLE source stopped on %s", pm_peer_name(s_ble_peer));
    s_ble_peer = NULL;
    s_ble_started = false;
}

static void _start_ble(void) {
    if (s_ble_peer) return;
    s_ble_peer = pm_peer_find("ble_scan", PM_PEER_ROLE_EXCLUSIVE);
    if (!s_ble_peer) {
        pm_log_w(TAG, "no BLE scan peer available");
        return;
    }
    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (kind == PM_PEER_KIND_CARDPUTER_I2C) {
        if (pm_peer_call(s_ble_peer, "ble_scan_start", "\"active\":0") == 0) {
            s_ble_started = true;
        } else {
            pm_peer_release_cap(s_ble_peer, "ble_scan");
            s_ble_peer = NULL;
            return;
        }
    } else if (kind == PM_PEER_KIND_TBEAM_S3) {
        if (pm_peer_call(s_ble_peer, "ble_scan_start", NULL) == 0) {
            s_ble_started = true;
        } else {
            pm_peer_release_cap(s_ble_peer, "ble_scan");
            s_ble_peer = NULL;
            return;
        }
    }
    pm_log_i(TAG, "BLE source: %s", pm_peer_name(s_ble_peer));
}

static void _poll_ble(void) {
    if (!s_session_active || !s_ble_peer) return;
    if (pm_peer_kind(s_ble_peer) != PM_PEER_KIND_CARDPUTER_I2C) return;
    for (int i = 0; i < 10; i++) {
        pm_cardputer_i2c_ble_seen_t b = {0};
        esp_err_t err = pm_cardputer_i2c_ble_seen_pop(&b);
        if (err != ESP_OK || !b.available) break;
        _on_ble(b.mac, b.name, b.rssi, b.mfg);
    }
}

// ── UI helpers ───────────────────────────────────────────
static const char* _kind_short(ts_kind_t k) {
    switch (k) {
        case TS_KIND_AIRTAG:            return "AIRTAG";
        case TS_KIND_TILE:              return "TILE";
        case TS_KIND_SMARTTAG:          return "SMARTTAG";
        case TS_KIND_SUSPECTED_APPLE:   return "APPLE?";
        case TS_KIND_SUSPECTED_TILE:    return "TILE?";
        case TS_KIND_SUSPECTED_SAMSUNG: return "SAMS?";
        default:                        return "?";
    }
}
static lv_color_t _kind_color(ts_kind_t k) {
    switch (k) {
        case TS_KIND_AIRTAG:   return PM_LAYOUT_COL_ERR;
        case TS_KIND_TILE:     return PM_LAYOUT_COL_GOLD;
        case TS_KIND_SMARTTAG: return PM_LAYOUT_COL_PURPLE;
        case TS_KIND_SUSPECTED_APPLE:
        case TS_KIND_SUSPECTED_TILE:
        case TS_KIND_SUSPECTED_SAMSUNG: return PM_LAYOUT_COL_WARN;
        default: return PM_LAYOUT_COL_DIM;
    }
}
static const char* _band_label(int rssi) {
    if (rssi >= -50) return "VERY CLOSE";
    if (rssi >= -65) return "NEAR";
    if (rssi >= -80) return "MEDIUM";
    return "FAR";
}
static lv_color_t _band_color(int rssi) {
    if (rssi >= -50) return PM_LAYOUT_COL_ERR;
    if (rssi >= -65) return PM_LAYOUT_COL_WARN;
    if (rssi >= -80) return PM_LAYOUT_COL_GOLD;
    return PM_LAYOUT_COL_OK;
}

// Format "Ns" "Nm" "Nh" duration shorthand.
static void _fmt_age(uint32_t ms, char* out, size_t cap) {
    uint32_t s = ms / 1000;
    if (s < 60)       { snprintf(out, cap, "%lus",  (unsigned long)s); return; }
    if (s < 3600)     { snprintf(out, cap, "%lum",  (unsigned long)(s / 60)); return; }
    snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
}

// ── UI state ─────────────────────────────────────────────
typedef struct {
    lv_obj_t* row;
    lv_obj_t* kind_lbl;
    lv_obj_t* kind_chip;
    lv_obj_t* mac_lbl;
    lv_obj_t* name_lbl;
    lv_obj_t* rssi_lbl;
    lv_obj_t* band_lbl;
    lv_obj_t* persist_lbl;
} ts_row_ui_t;

static ts_row_ui_t s_rows[MAX_TRACKERS];
static int         s_rows_created = 0;

static lv_obj_t* s_chip_status   = NULL;
static lv_obj_t* s_chip_alert    = NULL;
static lv_obj_t* s_stat_count    = NULL;
static lv_obj_t* s_stat_nearby   = NULL;
static lv_obj_t* s_stat_persist  = NULL;
static lv_obj_t* s_stat_strongest = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_advice_lbl    = NULL;

static lv_obj_t* s_screen = NULL;
static bool      s_built  = false;

// ── Row builder ──────────────────────────────────────────
static void _build_row(int idx, lv_obj_t* parent) {
    ts_row_ui_t* r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 44);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_column(r->row, 10, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    // Kind chip
    r->kind_chip = lv_obj_create(r->row);
    lv_obj_remove_style_all(r->kind_chip);
    lv_obj_set_size(r->kind_chip, 80, 22);
    lv_obj_set_style_bg_opa(r->kind_chip, 35, 0);
    lv_obj_set_style_border_width(r->kind_chip, 1, 0);
    lv_obj_set_style_radius(r->kind_chip, 3, 0);
    lv_obj_clear_flag(r->kind_chip, LV_OBJ_FLAG_SCROLLABLE);
    r->kind_lbl = lv_label_create(r->kind_chip);
    lv_label_set_text(r->kind_lbl, "—");
    lv_obj_set_style_text_font(r->kind_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_center(r->kind_lbl);

    // Two-line center column: name + MAC
    lv_obj_t* nm_box = lv_obj_create(r->row);
    lv_obj_remove_style_all(nm_box);
    lv_obj_set_flex_grow(nm_box, 1);
    lv_obj_set_height(nm_box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nm_box, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(nm_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nm_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(nm_box, 0, 0);
    lv_obj_clear_flag(nm_box, LV_OBJ_FLAG_SCROLLABLE);
    r->name_lbl = lv_label_create(nm_box);
    lv_label_set_text(r->name_lbl, "(unnamed)");
    lv_label_set_long_mode(r->name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->name_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->name_lbl, PM_LAYOUT_COL_FG_BR, 0);
    r->mac_lbl = lv_label_create(nm_box);
    lv_label_set_text(r->mac_lbl, "—:—:—:—:—:—");
    lv_obj_set_style_text_font(r->mac_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->mac_lbl, PM_LAYOUT_COL_DIM, 0);

    // Distance band
    r->band_lbl = lv_label_create(r->row);
    lv_label_set_text(r->band_lbl, "—");
    lv_obj_set_style_text_font(r->band_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->band_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->band_lbl, 96);
    lv_obj_set_style_text_align(r->band_lbl, LV_TEXT_ALIGN_CENTER, 0);

    // RSSI dBm
    r->rssi_lbl = lv_label_create(r->row);
    lv_label_set_text(r->rssi_lbl, "—");
    lv_obj_set_style_text_font(r->rssi_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->rssi_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_width(r->rssi_lbl, 70);
    lv_obj_set_style_text_align(r->rssi_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    // Persistence
    r->persist_lbl = lv_label_create(r->row);
    lv_label_set_text(r->persist_lbl, "—");
    lv_obj_set_style_text_font(r->persist_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->persist_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->persist_lbl, 60);
    lv_obj_set_style_text_align(r->persist_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

// ── Render ───────────────────────────────────────────────
static int _cmp_strongest(const void* a, const void* b) {
    int ia = *(const int*)a, ib = *(const int*)b;
    return s_devs[ib].latest_rssi - s_devs[ia].latest_rssi;
}

static void _render(void) {
    if (!s_built) return;

    uint32_t now = pm_millis();

    // Build sorted index of active devices.
    int sorted[MAX_TRACKERS];
    int count = 0;
    int nearby = 0;
    int persistent = 0;
    int best = 0;
    bool best_set = false;
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (!s_devs[i].active) continue;
        sorted[count++] = i;
        if (s_devs[i].latest_rssi >= -65) nearby++;
        if (now - s_devs[i].first_seen_ms > ALERT_AGE_MS) persistent++;
        if (!best_set || s_devs[i].latest_rssi > best) {
            best = s_devs[i].latest_rssi; best_set = true;
        }
    }
    if (count > 1) qsort(sorted, count, sizeof(int), _cmp_strongest);

    // Stats
    if (s_stat_count) {
        char b[8]; snprintf(b, sizeof(b), "%d", count);
        lv_label_set_text(s_stat_count, b);
    }
    if (s_stat_nearby) {
        char b[8]; snprintf(b, sizeof(b), "%d", nearby);
        lv_label_set_text(s_stat_nearby, b);
    }
    if (s_stat_persist) {
        char b[8]; snprintf(b, sizeof(b), "%d", persistent);
        lv_label_set_text(s_stat_persist, b);
        lv_obj_set_style_text_color(s_stat_persist,
            persistent ? PM_LAYOUT_COL_ERR : PM_LAYOUT_COL_FG_BR, 0);
    }
    if (s_stat_strongest) {
        char b[12];
        if (best_set) snprintf(b, sizeof(b), "%d", best);
        else          snprintf(b, sizeof(b), "—");
        lv_label_set_text(s_stat_strongest, b);
    }

    // Status / alert chips
    if (s_chip_status) {
        if (!s_session_active)  lv_label_set_text(s_chip_status, "STANDBY");
        else if (!s_ble_peer)   lv_label_set_text(s_chip_status, "NO RADIO");
        else                    lv_label_set_text(s_chip_status, "SCANNING");
    }
    if (s_chip_alert) {
        lv_color_t col = persistent > 0 ? PM_LAYOUT_COL_ERR
                       : count > 0      ? PM_LAYOUT_COL_WARN
                       :                  PM_LAYOUT_COL_OK;
        const char* txt = persistent > 0 ? "ALERT"
                       : count > 0       ? "TRACKERS NEAR"
                       :                   "CLEAR";
        lv_label_set_text(s_chip_alert, txt);
        lv_obj_set_style_text_color(s_chip_alert, col, 0);
        lv_obj_t* chip = lv_obj_get_parent(s_chip_alert);
        if (chip) {
            lv_obj_set_style_border_color(chip, col, 0);
            lv_obj_set_style_bg_color(chip, col, 0);
        }
    }

    // Advice line
    if (s_advice_lbl) {
        if (!s_session_active) {
            lv_label_set_text(s_advice_lbl, "Tap START to begin a passive tracker scan.");
        } else if (count == 0) {
            lv_label_set_text(s_advice_lbl,
                "No trackers detected nearby. Keep this app open while you move.");
        } else if (persistent > 0) {
            lv_label_set_text(s_advice_lbl,
                "A tracker has been near you for over 5 minutes. Investigate.");
        } else {
            lv_label_set_text(s_advice_lbl,
                "Trackers detected. Watch for persistence as you change locations.");
        }
    }

    // Rows
    int visible = 0;
    for (int i = 0; i < count && i < MAX_TRACKERS; i++) {
        const ts_dev_t* d = &s_devs[sorted[i]];
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        ts_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);

        lv_color_t kc = _kind_color(d->kind);
        lv_label_set_text(r->kind_lbl, _kind_short(d->kind));
        lv_obj_set_style_text_color(r->kind_lbl, kc, 0);
        lv_obj_set_style_border_color(r->kind_chip, kc, 0);
        lv_obj_set_style_bg_color(r->kind_chip, kc, 0);

        lv_label_set_text(r->name_lbl,
                           d->name[0] ? d->name : "(unnamed)");
        lv_obj_set_style_text_color(r->name_lbl,
            d->name[0] ? PM_LAYOUT_COL_FG_BR : PM_LAYOUT_COL_DIM, 0);
        lv_label_set_text(r->mac_lbl, d->mac);

        lv_color_t bc = _band_color(d->latest_rssi);
        lv_label_set_text(r->band_lbl, _band_label(d->latest_rssi));
        lv_obj_set_style_text_color(r->band_lbl, bc, 0);

        char rb[12];
        snprintf(rb, sizeof(rb), "%d dBm", d->latest_rssi);
        lv_label_set_text(r->rssi_lbl, rb);

        char pb[12];
        _fmt_age(now - d->first_seen_ms, pb, sizeof(pb));
        lv_label_set_text(r->persist_lbl, pb);
        bool alert = (now - d->first_seen_ms) > ALERT_AGE_MS;
        lv_obj_set_style_text_color(r->persist_lbl,
            alert ? PM_LAYOUT_COL_ERR : PM_LAYOUT_COL_DIM, 0);

        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Actions ──────────────────────────────────────────────
static void _act_start(lv_event_t* e) {
    (void)e;
    s_session_active = true;
    _start_ble();
}
static void _act_stop(lv_event_t* e) {
    (void)e;
    s_session_active = false;
    _stop_ble();
}
static void _act_clear(lv_event_t* e) {
    (void)e;
    memset(s_devs, 0, sizeof(s_devs));
}

// ── Screen build ─────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "TRACKER SCAN");

    s_chip_status = pm_app_layout_chip(&L, "STANDBY", PM_LAYOUT_COL_DIM);
    s_chip_alert  = pm_app_layout_chip(&L, "CLEAR",   PM_LAYOUT_COL_OK);

    pm_app_layout_stats_row(&L, 4);
    s_stat_count     = pm_app_layout_stat(&L, "TRACKERS",   "0");
    s_stat_nearby    = pm_app_layout_stat(&L, "NEAR",        "0");
    s_stat_persist   = pm_app_layout_stat(&L, "PERSISTENT",  "0");
    s_stat_strongest = pm_app_layout_stat(&L, "STRONGEST",   "—");

    pm_app_layout_content(&L);

    lv_obj_t* pane = pm_app_layout_pane(&L, 0, "SUSPECTED TRACKERS");
    s_list_box = lv_obj_create(pane);
    lv_obj_remove_style_all(s_list_box);
    lv_obj_set_width(s_list_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_list_box, 1);
    lv_obj_set_style_bg_opa(s_list_box, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_list_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list_box, 1, 0);
    lv_obj_add_flag(s_list_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_box, LV_SCROLLBAR_MODE_AUTO);

    // Advice footer
    lv_obj_t* footer = lv_obj_create(pane);
    lv_obj_remove_style_all(footer);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, 28);
    lv_obj_set_style_bg_color(footer, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(footer, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_hor(footer, 10, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    s_advice_lbl = lv_label_create(footer);
    lv_label_set_text(s_advice_lbl, "Tap START to begin a passive tracker scan.");
    lv_label_set_long_mode(s_advice_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_advice_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_advice_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_advice_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_align(s_advice_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    pm_app_layout_action(&L, "START", PM_LAYOUT_COL_OK,     _act_start);
    pm_app_layout_action(&L, "STOP",  PM_LAYOUT_COL_ERR,    _act_stop);
    pm_app_layout_action(&L, "CLEAR", PM_LAYOUT_COL_WARN,   _act_clear);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ────────────────────────────────────────────
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    _render();
}
static void _exit_(void) {
    s_session_active = false;
    _stop_ble();
}
static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    _poll_ble();
    uint32_t now = pm_millis();
    if (now - s_last_render < 400) return;
    s_last_render = now;
    _render();
}

static const pm_app_t _APP = {
    .id           = "tracker_scan",
    .display_name = "TRACKER SCAN",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_tracker_scan(void) { return &_APP; }
