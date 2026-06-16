// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_ble_beacon.c — BLE beacon spotter
//
//  Phase 19: spun off from pm_app_beacon (which now exclusively
//  handles WiFi AP discovery). This app parses BLE advertisement
//  manufacturer data for the three common beacon protocols:
//
//    iBeacon   — Apple. mfg = 4C 00 02 15 + 16-byte UUID +
//                2-byte major + 2-byte minor + 1-byte tx_power.
//    AltBeacon — Open. mfg = <comp_id_le> BE AC + 20-byte id +
//                1-byte tx_power + 1-byte mfg-reserved.
//    Eddystone — Google. Carried as a 16-bit UUID 0xFEAA
//                service-data block rather than mfg data;
//                detection here is name-prefix-based ("EDDY"
//                or service UUID hint in the name).
//
//  Display layout:
//    Pattern A (3-col cyber dashboard). Top stats: BEACONS /
//    IBEACON / EDDYSTONE / ALTBEACON. LEFT: scrollable list
//    sorted by RSSI desc. CENTER: selected-beacon detail panel
//    with full UUID/major/minor breakdown and a 60-sample RSSI
//    sparkline. RIGHT: type-breakdown stats and TX-power /
//    distance-estimate readouts.
//
//  RSSI → distance estimate (rough — depends on the beacon's
//  declared TX power at 1 m):
//      d ≈ 10 ^ ((txPower − rssi) / 20)
//  Anything inside ~2 m reads "near", under 10 m "mid", beyond
//  that "far". Walls, multipath and antenna direction make this
//  approximate at best, but it's the standard formula.
// ============================================================

#include "pm_app_ble_beacon.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = "PM_BLE_BEACON";

#define MAX_BEACONS       48
#define MAX_VISIBLE_ROWS  16
#define UUID_STR_LEN      37   // 8-4-4-4-12 + nul
#define MAC_LEN           18
#define NAME_LEN          32
#define RSSI_HISTORY_LEN  60

typedef enum {
    BTYPE_UNKNOWN  = 0,
    BTYPE_IBEACON  = 1,
    BTYPE_ALTBEACON= 2,
    BTYPE_EDDYSTONE= 3,
} btype_t;

typedef struct {
    btype_t type;
    char    mac[MAC_LEN];
    char    name[NAME_LEN];
    char    uuid[UUID_STR_LEN];
    uint16_t major;
    uint16_t minor;
    int8_t  tx_power;     // dBm @ 1m, declared by beacon
    int     rssi;
    uint32_t last_seen_ms;
} beacon_t;

static beacon_t s_beacons[MAX_BEACONS];
static int      s_beacon_count = 0;
static int      s_selected     = -1;
static SemaphoreHandle_t s_mtx = NULL;

static int  s_rssi_hist[RSSI_HISTORY_LEN];
static int  s_rssi_hist_count = 0;
static char s_rssi_hist_mac[MAC_LEN] = "";

static bool s_dirty = false;

// ── UI ─────────────────────────────────────────────────────
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_chip_state    = NULL;
static lv_obj_t* s_stat_total    = NULL;
static lv_obj_t* s_stat_ib       = NULL;
static lv_obj_t* s_stat_ed       = NULL;
static lv_obj_t* s_stat_alt      = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_list_header   = NULL;
static lv_obj_t* s_detail_lbl    = NULL;
static lv_obj_t* s_dist_lbl      = NULL;
static lv_obj_t* s_rssi_chart    = NULL;
static lv_chart_series_t* s_rssi_series = NULL;
static lv_obj_t* s_type_lbl      = NULL;
static bool      s_built         = false;

typedef struct {
    lv_obj_t* row;
    lv_obj_t* type_dot;
    lv_obj_t* name_lbl;
    lv_obj_t* meta_lbl;
    lv_obj_t* rssi_lbl;
} row_ui_t;
static row_ui_t s_rows[MAX_VISIBLE_ROWS];
static int      s_rows_created = 0;

// ── Helpers ────────────────────────────────────────────────
static void _lock(void)   { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void _unlock(void) { if (s_mtx) xSemaphoreGive(s_mtx); }

static lv_color_t _type_color(btype_t t) {
    switch (t) {
        case BTYPE_IBEACON:   return PM_LAYOUT_COL_ACCENT;
        case BTYPE_ALTBEACON: return PM_LAYOUT_COL_GOLD;
        case BTYPE_EDDYSTONE: return PM_LAYOUT_COL_OK;
        default:              return PM_LAYOUT_COL_FG_DIM;
    }
}

static const char* _type_name(btype_t t) {
    switch (t) {
        case BTYPE_IBEACON:   return "iBeacon";
        case BTYPE_ALTBEACON: return "AltBeacon";
        case BTYPE_EDDYSTONE: return "Eddystone";
        default:              return "Unknown";
    }
}

// Build the canonical 8-4-4-4-12 UUID string from 16 raw bytes.
static void _uuid_format(const uint8_t* b, char* out) {
    snprintf(out, UUID_STR_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

// ── Beacon parser ──────────────────────────────────────────
//
// Returns true if mfg_data is a recognized beacon advert,
// filling fields on `b`. Otherwise returns false; caller
// should drop the advert (it's not a beacon, just a regular
// BLE device).
static bool _parse_beacon(const uint8_t* mfg, uint8_t mfg_len,
                            const char* name, beacon_t* b) {
    b->type = BTYPE_UNKNOWN;
    b->uuid[0] = 0;
    b->major = b->minor = 0;
    b->tx_power = 0;

    if (mfg && mfg_len >= 25) {
        // iBeacon: 4C 00 02 15 + 16-byte UUID + major(2) + minor(2) + tx(1)
        // The 4C 00 is the Apple company ID (little-endian).
        if (mfg[0] == 0x4C && mfg[1] == 0x00 &&
            mfg[2] == 0x02 && mfg[3] == 0x15) {
            b->type = BTYPE_IBEACON;
            _uuid_format(&mfg[4], b->uuid);
            b->major    = (uint16_t)((mfg[20] << 8) | mfg[21]);
            b->minor    = (uint16_t)((mfg[22] << 8) | mfg[23]);
            b->tx_power = (int8_t)mfg[24];
            return true;
        }
    }
    if (mfg && mfg_len >= 24) {
        // AltBeacon: <comp_id_le> BE AC + 20 bytes id + tx(1) + rsvd(1)
        // The 0xBEAC marker is the differentiator; company ID varies.
        if (mfg[2] == 0xBE && mfg[3] == 0xAC) {
            b->type = BTYPE_ALTBEACON;
            // First 16 of the 20-byte id treated as UUID for display.
            _uuid_format(&mfg[4], b->uuid);
            b->major    = (uint16_t)((mfg[20] << 8) | mfg[21]);
            b->minor    = (uint16_t)((mfg[22] << 8) | mfg[23]);
            b->tx_power = (int8_t)((mfg_len >= 25) ? mfg[24] : 0);
            return true;
        }
    }
    // Eddystone: we don't have service-data here, so fall back to
    // name-prefix sniffing. Modern Eddystone implementations
    // commonly include "Eddystone" or "EDDY" in the advert name.
    if (name && name[0]) {
        if (strncasecmp(name, "Eddy", 4) == 0 ||
            strstr(name, "EDDY") != NULL) {
            b->type = BTYPE_EDDYSTONE;
            return true;
        }
    }
    return false;
}

// ── Public callback from main.c ────────────────────────────
void pm_app_ble_beacon_on_adv(const char* mac, const char* name,
                                int rssi,
                                const uint8_t* mfg_data, uint8_t mfg_len) {
    beacon_t parsed = {0};
    if (!_parse_beacon(mfg_data, mfg_len, name, &parsed)) return;

    if (mac) {
        strncpy(parsed.mac, mac, MAC_LEN - 1);
        parsed.mac[MAC_LEN - 1] = 0;
    }
    if (name) {
        strncpy(parsed.name, name, NAME_LEN - 1);
        parsed.name[NAME_LEN - 1] = 0;
    }
    parsed.rssi = rssi;
    parsed.last_seen_ms = pm_millis();

    _lock();
    // Find existing entry by MAC.
    int idx = -1;
    for (int i = 0; i < s_beacon_count; i++) {
        if (strcmp(s_beacons[i].mac, parsed.mac) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (s_beacon_count < MAX_BEACONS) {
            idx = s_beacon_count++;
        } else {
            // Replace the stalest entry — keep the table interesting.
            uint32_t oldest = UINT32_MAX;
            for (int i = 0; i < s_beacon_count; i++) {
                if (s_beacons[i].last_seen_ms < oldest) {
                    oldest = s_beacons[i].last_seen_ms;
                    idx = i;
                }
            }
        }
    }
    if (idx >= 0) {
        s_beacons[idx] = parsed;
    }
    _unlock();
    s_dirty = true;
}

// ── Row UI ─────────────────────────────────────────────────
static void _row_clicked(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    // slot maps to the i-th visible beacon; we need the global index.
    _lock();
    if (slot >= 0 && slot < s_beacon_count) {
        s_selected = slot;
        if (strcmp(s_beacons[slot].mac, s_rssi_hist_mac) != 0) {
            strncpy(s_rssi_hist_mac, s_beacons[slot].mac, MAC_LEN - 1);
            s_rssi_hist_mac[MAC_LEN - 1] = 0;
            s_rssi_hist_count = 0;
        }
    }
    _unlock();
    s_dirty = true;
}

static void _build_row(int slot) {
    row_ui_t* r = &s_rows[slot];
    r->row = lv_obj_create(s_list_box);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 44);
    lv_obj_set_style_bg_color(r->row, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r->row, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_ver(r->row, 6, 0);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->row, _row_clicked, LV_EVENT_CLICKED,
                         (void*)(intptr_t)slot);

    r->type_dot = lv_obj_create(r->row);
    lv_obj_remove_style_all(r->type_dot);
    lv_obj_set_size(r->type_dot, 10, 10);
    lv_obj_set_style_radius(r->type_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r->type_dot, PM_LAYOUT_COL_FG_DIM, 0);
    lv_obj_set_style_bg_opa(r->type_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(r->type_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(r->type_dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* col = lv_obj_create(r->row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    r->name_lbl = lv_label_create(col);
    lv_label_set_text(r->name_lbl, "—");
    lv_label_set_long_mode(r->name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->name_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->name_lbl, PM_LAYOUT_COL_FG_BR, 0);

    r->meta_lbl = lv_label_create(col);
    lv_label_set_text(r->meta_lbl, "");
    lv_label_set_long_mode(r->meta_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->meta_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->meta_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->meta_lbl, PM_LAYOUT_COL_FG_DIM, 0);

    r->rssi_lbl = lv_label_create(r->row);
    lv_label_set_text(r->rssi_lbl, "—");
    lv_obj_set_style_text_font(r->rssi_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->rssi_lbl, PM_LAYOUT_COL_GOLD, 0);
}

static void _render_row(int slot, int idx, bool selected) {
    row_ui_t* r = &s_rows[slot];
    const beacon_t* b = &s_beacons[idx];
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    if (selected) {
        lv_obj_set_style_bg_color(r->row, PM_LAYOUT_COL_BG3, 0);
        lv_obj_set_style_border_color(r->row, PM_LAYOUT_COL_ACCENT, 0);
        lv_obj_set_style_border_width(r->row, 2, 0);
        lv_obj_set_style_border_side(r->row,
            LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM, 0);
    } else {
        lv_obj_set_style_bg_color(r->row, PM_LAYOUT_COL_BG, 0);
        lv_obj_set_style_border_color(r->row, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(r->row, 1, 0);
        lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    }
    lv_obj_set_style_bg_color(r->type_dot, _type_color(b->type), 0);
    lv_label_set_text(r->name_lbl,
        b->name[0] ? b->name : b->mac);
    char meta[64];
    snprintf(meta, sizeof(meta), "%s  %s",
              _type_name(b->type), b->mac);
    lv_label_set_text(r->meta_lbl, meta);
    char rb[16];
    snprintf(rb, sizeof(rb), "%d", b->rssi);
    lv_label_set_text(r->rssi_lbl, rb);
}

// ── Render ─────────────────────────────────────────────────
static int _cmp_rssi_desc(const void* a, const void* b) {
    const beacon_t* x = (const beacon_t*)a;
    const beacon_t* y = (const beacon_t*)b;
    return y->rssi - x->rssi;
}

static void _render(void) {
    if (!s_built) return;

    _lock();
    qsort(s_beacons, s_beacon_count, sizeof(beacon_t), _cmp_rssi_desc);

    int n_ib = 0, n_ed = 0, n_alt = 0;
    for (int i = 0; i < s_beacon_count; i++) {
        switch (s_beacons[i].type) {
            case BTYPE_IBEACON:   n_ib++;  break;
            case BTYPE_ALTBEACON: n_alt++; break;
            case BTYPE_EDDYSTONE: n_ed++;  break;
            default: break;
        }
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", s_beacon_count);
    if (s_stat_total) lv_label_set_text(s_stat_total, buf);
    snprintf(buf, sizeof(buf), "%d", n_ib);
    if (s_stat_ib) { lv_label_set_text(s_stat_ib, buf);
        pm_app_layout_stat_color(s_stat_ib, PM_LAYOUT_COL_ACCENT); }
    snprintf(buf, sizeof(buf), "%d", n_ed);
    if (s_stat_ed) { lv_label_set_text(s_stat_ed, buf);
        pm_app_layout_stat_color(s_stat_ed, PM_LAYOUT_COL_OK); }
    snprintf(buf, sizeof(buf), "%d", n_alt);
    if (s_stat_alt) { lv_label_set_text(s_stat_alt, buf);
        pm_app_layout_stat_color(s_stat_alt, PM_LAYOUT_COL_GOLD); }

    if (s_chip_state) {
        if (s_beacon_count > 0) {
            lv_label_set_text(s_chip_state, "TRACKING");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_OK, 0);
        } else {
            lv_label_set_text(s_chip_state, "LISTENING");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_ACCENT, 0);
        }
    }
    if (s_list_header) {
        char hb[24]; snprintf(hb, sizeof(hb), "%d", s_beacon_count);
        lv_label_set_text(s_list_header, hb);
    }

    int visible = 0;
    for (int i = 0; i < s_beacon_count && i < MAX_VISIBLE_ROWS; i++) {
        if (visible >= s_rows_created) {
            _build_row(s_rows_created);
            s_rows_created++;
        }
        _render_row(visible, i, i == s_selected);
        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    // Detail
    if (s_selected >= 0 && s_selected < s_beacon_count) {
        const beacon_t* b = &s_beacons[s_selected];
        char dbuf[512];
        snprintf(dbuf, sizeof(dbuf),
            "TYPE:    %s\n"
            "NAME:    %s\n"
            "MAC:     %s\n"
            "UUID:    %s\n"
            "MAJOR:   %u    MINOR:   %u\n"
            "TX@1m:   %d dBm    RSSI: %d dBm",
            _type_name(b->type),
            b->name[0] ? b->name : "(none)",
            b->mac,
            b->uuid[0] ? b->uuid : "(none)",
            (unsigned)b->major, (unsigned)b->minor,
            (int)b->tx_power, b->rssi);
        if (s_detail_lbl) lv_label_set_text(s_detail_lbl, dbuf);

        // Distance estimate using the standard iBeacon formula.
        // If tx_power isn't declared we fall back to a calibrated
        // guess (-59 dBm @ 1 m is a common iBeacon default).
        int tx = b->tx_power != 0 ? b->tx_power : -59;
        double dist_m = pow(10.0, ((double)tx - (double)b->rssi) / 20.0);
        const char* zone =
            (dist_m < 2.0)  ? "NEAR" :
            (dist_m < 10.0) ? "MID"  : "FAR";
        char distbuf[64];
        snprintf(distbuf, sizeof(distbuf),
            "≈ %.1f m   ZONE: %s", dist_m, zone);
        if (s_dist_lbl) {
            lv_label_set_text(s_dist_lbl, distbuf);
            lv_obj_set_style_text_color(s_dist_lbl,
                (dist_m < 2.0) ? PM_LAYOUT_COL_OK :
                (dist_m < 10.0) ? PM_LAYOUT_COL_WARN :
                                   PM_LAYOUT_COL_ERR, 0);
        }

        // Sparkline
        if (s_rssi_chart && s_rssi_series) {
            if (strcmp(b->mac, s_rssi_hist_mac) != 0) {
                strncpy(s_rssi_hist_mac, b->mac, MAC_LEN - 1);
                s_rssi_hist_mac[MAC_LEN - 1] = 0;
                s_rssi_hist_count = 0;
            }
            if (s_rssi_hist_count < RSSI_HISTORY_LEN) {
                s_rssi_hist[s_rssi_hist_count++] = b->rssi;
            } else {
                memmove(&s_rssi_hist[0], &s_rssi_hist[1],
                         sizeof(int) * (RSSI_HISTORY_LEN - 1));
                s_rssi_hist[RSSI_HISTORY_LEN - 1] = b->rssi;
            }
            for (int i = 0; i < s_rssi_hist_count; i++) {
                lv_chart_set_value_by_id(s_rssi_chart, s_rssi_series,
                                          i, s_rssi_hist[i]);
            }
            lv_chart_refresh(s_rssi_chart);
        }
    } else if (s_detail_lbl) {
        lv_label_set_text(s_detail_lbl, "Tap a beacon on the left.");
        if (s_dist_lbl) lv_label_set_text(s_dist_lbl, "—");
    }

    // Type breakdown text on right pane
    if (s_type_lbl) {
        char tb[128];
        snprintf(tb, sizeof(tb),
            "#4dd9ff iBEACON %d#\n#ffd166 ALTBEACON %d#\n#4dffa6 EDDYSTONE %d#",
            n_ib, n_alt, n_ed);
        lv_label_set_text(s_type_lbl, tb);
        lv_label_set_recolor(s_type_lbl, true);
    }

    _unlock();
}

// ── Screen build ───────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "BLE BEACON SPOTTER");

    s_chip_state = pm_app_layout_chip(&L, "LISTENING", PM_LAYOUT_COL_ACCENT);
    pm_app_layout_chip(&L, "C6 BLE", PM_LAYOUT_COL_FG_DIM);

    pm_app_layout_stats_row(&L, 4);
    s_stat_total = pm_app_layout_stat(&L, "BEACONS",   "0");
    s_stat_ib    = pm_app_layout_stat(&L, "IBEACON",   "0");
    s_stat_ed    = pm_app_layout_stat(&L, "EDDYSTONE", "0");
    s_stat_alt   = pm_app_layout_stat(&L, "ALTBEACON", "0");

    pm_app_layout_content(&L);

#if PM_BOARD_LCD_H_RES <= 800
    int left_w  = 280;
    int right_w = 240;
#else
    int left_w  = 360;
    int right_w = 300;
#endif

    // LEFT — list
    lv_obj_t* left = pm_app_layout_pane(&L, left_w, NULL);
    lv_obj_t* sh = pm_app_layout_section_header(left, "BEACONS", "0");
    if (sh && lv_obj_get_child_count(sh) > 1) {
        s_list_header = lv_obj_get_child(sh, 1);
    }
    s_list_box = lv_obj_create(left);
    lv_obj_remove_style_all(s_list_box);
    lv_obj_set_width(s_list_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_list_box, 1);
    lv_obj_set_style_bg_opa(s_list_box, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_list_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list_box, 0, 0);
    lv_obj_add_flag(s_list_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_box, LV_SCROLLBAR_MODE_AUTO);

    // CENTER — detail + sparkline
    lv_obj_t* center = pm_app_layout_pane(&L, 0, NULL);
    pm_app_layout_section_header(center, "DETAIL", NULL);
    s_detail_lbl = lv_label_create(center);
    lv_label_set_text(s_detail_lbl, "Tap a beacon on the left.");
    lv_label_set_long_mode(s_detail_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail_lbl, LV_PCT(100));
    lv_obj_set_style_pad_all(s_detail_lbl, 14, 0);
    lv_obj_set_style_text_font(s_detail_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_lbl, PM_LAYOUT_COL_FG, 0);
    lv_obj_set_style_text_line_space(s_detail_lbl, 4, 0);

    lv_obj_t* sec = pm_app_layout_chart_section(center,
        "RSSI HISTORY — SELECTED");
    s_rssi_chart = lv_chart_create(sec);
    lv_obj_set_size(s_rssi_chart, LV_PCT(100), 90);
    lv_chart_set_type(s_rssi_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_rssi_chart, RSSI_HISTORY_LEN);
    lv_chart_set_range(s_rssi_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_obj_set_style_bg_color(s_rssi_chart, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_rssi_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_rssi_chart, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(s_rssi_chart, 1, 0);
    lv_chart_set_div_line_count(s_rssi_chart, 3, 0);
    s_rssi_series = lv_chart_add_series(s_rssi_chart,
        PM_LAYOUT_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_size(s_rssi_chart, 0, 0, LV_PART_INDICATOR);

    // RIGHT — type breakdown + distance
    lv_obj_t* right = pm_app_layout_pane(&L, right_w, NULL);
    pm_app_layout_section_header(right, "TYPE BREAKDOWN", NULL);
    s_type_lbl = lv_label_create(right);
    lv_label_set_text(s_type_lbl, "—");
    lv_label_set_long_mode(s_type_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_type_lbl, LV_PCT(100));
    lv_obj_set_style_pad_all(s_type_lbl, 14, 0);
    lv_obj_set_style_text_font(s_type_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_line_space(s_type_lbl, 6, 0);
    lv_label_set_recolor(s_type_lbl, true);

    pm_app_layout_section_header(right, "DISTANCE", NULL);
    s_dist_lbl = lv_label_create(right);
    lv_label_set_text(s_dist_lbl, "—");
    lv_obj_set_width(s_dist_lbl, LV_PCT(100));
    lv_obj_set_style_pad_all(s_dist_lbl, 14, 0);
    lv_obj_set_style_text_font(s_dist_lbl, PM_LAYOUT_FONT_STAT, 0);
    lv_obj_set_style_text_color(s_dist_lbl, PM_LAYOUT_COL_GOLD, 0);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ──────────────────────────────────────────────
static void _init(void) {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    _render();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 350) return;
    s_last_render_ms = now;
    if (s_dirty) { s_dirty = false; _render(); }
}

static void _exit_(void) {}

static const pm_app_t _APP = {
    .id           = "ble_beacon",
    .display_name = "BLE BEACON",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_ble_beacon(void) { return &_APP; }
