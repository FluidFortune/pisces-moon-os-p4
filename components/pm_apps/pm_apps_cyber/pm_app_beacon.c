// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_beacon.c — WiFi AP intelligence dashboard
//
//  Data layer:
//    - Tracks up to MAX_APS unique BSSIDs.
//    - Each AP carries SSID, RSSI, channel, encryption, hits,
//      and last-seen timestamp.
//    - When the table fills, the oldest-seen slot is recycled.
//      (Earlier versions accidentally truncated s_count when
//      recycling — that's now fixed; we just zero+reuse the slot.)
//
//  UI:
//    - Dashboard scaffold via pm_app_layout (titlebar + chips +
//      stats + content split + action bar).
//    - Left pane:  scrollable AP table. Each row = channel badge,
//                  RSSI bar with colored fill, encryption badge,
//                  BSSID (mono), SSID, hits.
//    - Right pane: 2.4 GHz channel histogram (channels 1-13)
//                  with bar height proportional to AP count per
//                  channel.
//    - Tick rate:  every 500 ms re-sorts the AP table by RSSI
//                  descending and refreshes labels in place.
//
//  Wiring:
//    - Public input remains pm_app_beacon_on_wifi(...). Any
//      scanner — Wardrive's WiFi owner, the C6 scan command, a
//      cardputer relay — can push frames in via that call.
//    - The CLEAR action wipes the table. EXPORT/FILTER are
//      stubbed for now (logged but not implemented yet).
// ============================================================

#include "pm_app_beacon.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

static const char* TAG = "PM_BEACON";

// ── Data model ───────────────────────────────────────────
#define MAX_APS         96
#define MAX_VISIBLE     48   // most we'll render as UI rows
#define HIST_CHANNELS   14   // 2.4 GHz (channel 0 unused, 1..13)

typedef struct {
    char     bssid[18];
    char     ssid[33];
    int      rssi;
    int      channel;
    char     enc[12];
    uint32_t last_seen_ms;
    int      hits;
} ap_t;

static ap_t s_aps[MAX_APS];
static int  s_count = 0;

// Filter modes — cycled by the FILTER action button.
typedef enum {
    BCN_FILTER_ALL = 0,
    BCN_FILTER_OPEN,
    BCN_FILTER_SECURED,
    BCN_FILTER_COUNT
} bcn_filter_t;
static bcn_filter_t s_filter = BCN_FILTER_ALL;

// ── Public input from the scanner side ───────────────────
void pm_app_beacon_on_wifi(const char* bssid, const char* ssid,
                            int rssi, int channel, const char* enc) {
    if (!bssid) return;

    // Existing AP — refresh signal & timestamp.
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_aps[i].bssid, bssid) == 0) {
            s_aps[i].rssi         = rssi;
            s_aps[i].channel      = channel;
            s_aps[i].last_seen_ms = pm_millis();
            s_aps[i].hits++;
            if (enc && enc[0]) {
                strncpy(s_aps[i].enc, enc, sizeof(s_aps[i].enc) - 1);
                s_aps[i].enc[sizeof(s_aps[i].enc) - 1] = '\0';
            }
            if (ssid && ssid[0]) {
                strncpy(s_aps[i].ssid, ssid, sizeof(s_aps[i].ssid) - 1);
                s_aps[i].ssid[sizeof(s_aps[i].ssid) - 1] = '\0';
            }
            return;
        }
    }

    // New AP. If we're full, recycle the oldest-seen slot in place
    // (do NOT shrink s_count — the earlier code did that and lost
    // every slot above `oldest`).
    int slot;
    if (s_count >= MAX_APS) {
        slot = 0;
        for (int i = 1; i < s_count; i++) {
            if (s_aps[i].last_seen_ms < s_aps[slot].last_seen_ms) slot = i;
        }
        memset(&s_aps[slot], 0, sizeof(ap_t));
    } else {
        slot = s_count++;
    }

    ap_t* a = &s_aps[slot];
    strncpy(a->bssid, bssid,                   sizeof(a->bssid) - 1);
    strncpy(a->ssid,  ssid ? ssid : "",        sizeof(a->ssid)  - 1);
    strncpy(a->enc,   enc  ? enc  : "?",       sizeof(a->enc)   - 1);
    a->rssi         = rssi;
    a->channel      = channel;
    a->last_seen_ms = pm_millis();
    a->hits         = 1;
}

// ── Visual helpers ───────────────────────────────────────

// Map an encryption string to a category color.
//
// We accept whatever the scanner produces — common values include
// "OPEN", "WEP", "WPA", "WPA2", "WPA2_PSK", "WPA3", "WPA2/WPA3".
// Test the most-specific tokens first.
static lv_color_t _enc_color(const char* enc) {
    if (!enc || !*enc)                return PM_LAYOUT_COL_DIM;
    if (strcasecmp(enc, "?") == 0)    return PM_LAYOUT_COL_DIM;
    if (strstr(enc, "WPA3") || strstr(enc, "wpa3")) return PM_LAYOUT_COL_PURPLE;
    if (strstr(enc, "WPA2") || strstr(enc, "wpa2")) return PM_LAYOUT_COL_OK;
    if (strstr(enc, "WPA")  || strstr(enc, "wpa"))  return PM_LAYOUT_COL_ACCENT;
    if (strstr(enc, "WEP")  || strstr(enc, "wep"))  return PM_LAYOUT_COL_WARN;
    if (strstr(enc, "OPEN") || strstr(enc, "open")
        || strstr(enc, "NONE") || strstr(enc, "none")) return PM_LAYOUT_COL_ERR;
    return PM_LAYOUT_COL_DIM;
}

// Open-network predicate — used by filter logic and the OPEN tally.
// Mirrors the OPEN branch of _enc_color but returns a bool so we
// don't have to compare lv_color_t structs.
static bool _enc_is_open(const char* enc) {
    if (!enc || !*enc) return false;
    if (strcmp(enc, "?") == 0) return false;
    if (strstr(enc, "OPEN") || strstr(enc, "open")) return true;
    if (strstr(enc, "NONE") || strstr(enc, "none")) return true;
    return false;
}

// Secured predicate — anything we can identify as WEP/WPA/WPA2/WPA3.
static bool _enc_is_secured(const char* enc) {
    if (!enc || !*enc) return false;
    if (strcmp(enc, "?") == 0) return false;
    if (_enc_is_open(enc)) return false;
    if (strstr(enc, "WPA") || strstr(enc, "wpa")) return true;
    if (strstr(enc, "WEP") || strstr(enc, "wep")) return true;
    return false;
}

// Short label for the enc badge — collapses "WPA2_PSK" → "WPA2" etc.
static const char* _enc_short(const char* enc) {
    if (!enc || !*enc || strcmp(enc, "?") == 0) return "?";
    if (strstr(enc, "WPA3") || strstr(enc, "wpa3")) return "WPA3";
    if (strstr(enc, "WPA2") || strstr(enc, "wpa2")) return "WPA2";
    if (strstr(enc, "WPA")  || strstr(enc, "wpa"))  return "WPA";
    if (strstr(enc, "WEP")  || strstr(enc, "wep"))  return "WEP";
    if (strstr(enc, "OPEN") || strstr(enc, "open")) return "OPEN";
    if (strstr(enc, "NONE") || strstr(enc, "none")) return "OPEN";
    return enc;
}

// Map RSSI dBm → bar fill colour.
static lv_color_t _rssi_color(int rssi) {
    if (rssi >= -55) return PM_LAYOUT_COL_OK;     // strong
    if (rssi >= -67) return PM_LAYOUT_COL_GOLD;   // good
    if (rssi >= -78) return PM_LAYOUT_COL_WARN;   // fair
    return PM_LAYOUT_COL_ERR;                      // weak
}

// Map RSSI dBm → 0..100 percentage for the bar.
//
// Useful range is roughly -90 dBm (noise floor) to -30 dBm (very
// close). Clip into that range.
static int _rssi_pct(int rssi) {
    int v = rssi + 90;
    if (v < 0)  v = 0;
    if (v > 60) v = 60;
    return (v * 100) / 60;
}

// Does this AP pass the current filter?
static bool _filter_accepts(const ap_t* a) {
    if (s_filter == BCN_FILTER_ALL)     return true;
    if (s_filter == BCN_FILTER_OPEN)    return  _enc_is_open(a->enc);
    if (s_filter == BCN_FILTER_SECURED) return !_enc_is_open(a->enc);
    return true;
}

// ── UI state ─────────────────────────────────────────────
//
// Static row widgets sized for MAX_VISIBLE. We never destroy
// these — on each render we update labels and toggle visibility.
typedef struct {
    lv_obj_t* row;
    lv_obj_t* ch_lbl;
    lv_obj_t* rssi_bar;
    lv_obj_t* rssi_lbl;
    lv_obj_t* enc_lbl;
    lv_obj_t* enc_chip;
    lv_obj_t* bssid_lbl;
    lv_obj_t* ssid_lbl;
    lv_obj_t* hits_lbl;
} ap_row_ui_t;

static ap_row_ui_t s_rows[MAX_VISIBLE];
static int         s_rows_created = 0;

// Header chips and stats — created at build time, updated on tick.
static lv_obj_t* s_chip_status      = NULL;
static lv_obj_t* s_chip_filter      = NULL;
static lv_obj_t* s_stat_count       = NULL;
static lv_obj_t* s_stat_channels    = NULL;
static lv_obj_t* s_stat_best        = NULL;
static lv_obj_t* s_stat_open        = NULL;
static lv_obj_t* s_stat_secured     = NULL;
static lv_obj_t* s_stat_hidden      = NULL;

// Channel histogram bars (right pane).
static lv_obj_t* s_hist_bars[HIST_CHANNELS] = {0};
static lv_obj_t* s_hist_lbls[HIST_CHANNELS] = {0};

// Scrollable list container that hosts s_rows[*].row children.
static lv_obj_t* s_ap_list  = NULL;
static lv_obj_t* s_hist_box = NULL;

// Whether the screen is built. Apps lazy-build at first _enter.
static lv_obj_t* s_screen = NULL;
static bool      s_built  = false;

// ── Row builder ──────────────────────────────────────────
//
// Each row is a horizontal flex container. Widget order:
//   channel badge | RSSI bar + label | enc chip | BSSID | SSID | hits
//
// We size the row to a fixed height; columns either get fixed
// widths or flex-grow.
static void _build_row(int idx, lv_obj_t* parent) {
    ap_row_ui_t* r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 30);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 8, 0);
    lv_obj_set_style_pad_ver(r->row, 2, 0);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    // Channel badge
    lv_obj_t* ch_box = lv_obj_create(r->row);
    lv_obj_remove_style_all(ch_box);
    lv_obj_set_size(ch_box, 32, 22);
    lv_obj_set_style_bg_color(ch_box, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(ch_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ch_box, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_set_style_border_width(ch_box, 1, 0);
    lv_obj_set_style_radius(ch_box, 3, 0);
    lv_obj_clear_flag(ch_box, LV_OBJ_FLAG_SCROLLABLE);
    r->ch_lbl = lv_label_create(ch_box);
    lv_label_set_text(r->ch_lbl, "—");
    lv_obj_set_style_text_font(r->ch_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->ch_lbl, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_center(r->ch_lbl);

    // RSSI bar + label in a small column
    lv_obj_t* rssi_box = lv_obj_create(r->row);
    lv_obj_remove_style_all(rssi_box);
    lv_obj_set_size(rssi_box, 130, 22);
    lv_obj_set_style_bg_opa(rssi_box, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(rssi_box, LV_OBJ_FLAG_SCROLLABLE);
    r->rssi_bar = lv_bar_create(rssi_box);
    lv_obj_remove_style_all(r->rssi_bar);
    lv_obj_set_size(r->rssi_bar, 130, 14);
    lv_obj_align(r->rssi_bar, LV_ALIGN_TOP_LEFT, 0, 0);
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
    lv_obj_align(r->rssi_lbl, LV_ALIGN_TOP_RIGHT, -4, 0);

    // Encryption chip
    r->enc_chip = lv_obj_create(r->row);
    lv_obj_remove_style_all(r->enc_chip);
    lv_obj_set_size(r->enc_chip, 52, 18);
    lv_obj_set_style_bg_color(r->enc_chip, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_bg_opa(r->enc_chip, 30, 0);
    lv_obj_set_style_border_color(r->enc_chip, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_border_width(r->enc_chip, 1, 0);
    lv_obj_set_style_radius(r->enc_chip, 3, 0);
    lv_obj_clear_flag(r->enc_chip, LV_OBJ_FLAG_SCROLLABLE);
    r->enc_lbl = lv_label_create(r->enc_chip);
    lv_label_set_text(r->enc_lbl, "?");
    lv_obj_set_style_text_font(r->enc_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->enc_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_center(r->enc_lbl);

    // BSSID — monospace-ish via dim color
    r->bssid_lbl = lv_label_create(r->row);
    lv_label_set_text(r->bssid_lbl, "--:--:--:--:--:--");
    lv_obj_set_style_text_font(r->bssid_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->bssid_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->bssid_lbl, 140);

    // SSID — flex-grow takes the slack
    r->ssid_lbl = lv_label_create(r->row);
    lv_label_set_text(r->ssid_lbl, "(empty)");
    lv_label_set_long_mode(r->ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(r->ssid_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->ssid_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_flex_grow(r->ssid_lbl, 1);

    // Hits
    r->hits_lbl = lv_label_create(r->row);
    lv_label_set_text(r->hits_lbl, "0");
    lv_obj_set_style_text_font(r->hits_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->hits_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(r->hits_lbl, 32);
    lv_obj_set_style_text_align(r->hits_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

// ── Histogram (right pane) ───────────────────────────────
static void _build_histogram(lv_obj_t* pane) {
    // Pane is flex-column; insert a row of bars.
    lv_obj_t* hdr = lv_label_create(pane);
    lv_label_set_text(hdr, "2.4 GHz CHANNEL ACTIVITY");
    lv_obj_set_style_text_font(hdr, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(hdr, 1, 0);
    lv_obj_set_style_pad_top(hdr, 8, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);

    s_hist_box = lv_obj_create(pane);
    lv_obj_remove_style_all(s_hist_box);
    lv_obj_set_width(s_hist_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_hist_box, 1);
    lv_obj_set_style_bg_opa(s_hist_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_hist_box, 8, 0);
    lv_obj_set_style_pad_column(s_hist_box, 4, 0);
    lv_obj_set_layout(s_hist_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_hist_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_hist_box, LV_FLEX_ALIGN_SPACE_BETWEEN,
                           LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_clear_flag(s_hist_box, LV_OBJ_FLAG_SCROLLABLE);

    // Channels 1..13
    for (int c = 1; c < HIST_CHANNELS; c++) {
        lv_obj_t* col = lv_obj_create(s_hist_box);
        lv_obj_remove_style_all(col);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_layout(col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END,
                               LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        s_hist_bars[c] = lv_obj_create(col);
        lv_obj_remove_style_all(s_hist_bars[c]);
        lv_obj_set_width(s_hist_bars[c], LV_PCT(80));
        lv_obj_set_height(s_hist_bars[c], 4);
        lv_obj_set_style_bg_color(s_hist_bars[c], PM_LAYOUT_COL_ACCENT, 0);
        lv_obj_set_style_bg_opa(s_hist_bars[c], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_hist_bars[c], 1, 0);

        s_hist_lbls[c] = lv_label_create(col);
        char nb[4];
        snprintf(nb, sizeof(nb), "%d", c);
        lv_label_set_text(s_hist_lbls[c], nb);
        lv_obj_set_style_text_font(s_hist_lbls[c], PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(s_hist_lbls[c], PM_LAYOUT_COL_DIM, 0);
        lv_obj_set_style_pad_top(s_hist_lbls[c], 2, 0);
    }
}

// ── Action callbacks ─────────────────────────────────────
static void _act_clear(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "clear");
    s_count = 0;
    memset(s_aps, 0, sizeof(s_aps));
}

static void _act_export(lv_event_t* e) {
    (void)e;
    pm_log_i(TAG, "export (TODO — route to wardrive CSV)");
}

static void _act_filter(lv_event_t* e) {
    (void)e;
    s_filter = (bcn_filter_t)((s_filter + 1) % BCN_FILTER_COUNT);
    if (s_chip_filter) {
        const char* name = (s_filter == BCN_FILTER_OPEN)    ? "OPEN ONLY"
                          : (s_filter == BCN_FILTER_SECURED) ? "SECURED"
                          : "ALL";
        lv_label_set_text(s_chip_filter, name);
    }
    pm_log_i(TAG, "filter -> %d", (int)s_filter);
}

// ── Build the screen (lazy, first _enter) ────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "BEACON");

    // Header chips
    s_chip_status = pm_app_layout_chip(&L, "READY",     PM_LAYOUT_COL_OK);
    s_chip_filter = pm_app_layout_chip(&L, "ALL",       PM_LAYOUT_COL_ACCENT);

    // Stats row — 6 cells on the 7" (also fine on 5",
    // pm_app_layout_stats_row clamps to 8 max).
    pm_app_layout_stats_row(&L, 6);
    s_stat_count    = pm_app_layout_stat(&L, "APS",       "0");
    s_stat_channels = pm_app_layout_stat(&L, "CHANNELS",  "0");
    s_stat_best     = pm_app_layout_stat(&L, "STRONGEST", "—");
    s_stat_open     = pm_app_layout_stat(&L, "OPEN",      "0");
    s_stat_secured  = pm_app_layout_stat(&L, "SECURED",   "0");
    s_stat_hidden   = pm_app_layout_stat(&L, "HIDDEN",    "0");

    // Content split: AP list (flex-grow) | histogram (fixed)
    pm_app_layout_content(&L);

    // Left pane: scrollable AP list
    lv_obj_t* list_pane = pm_app_layout_pane(&L, 0, "DETECTED APS");
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

    // Right pane: channel histogram
#if PM_BOARD_LCD_H_RES <= 800
    int hist_w = 220;
#else
    int hist_w = 320;
#endif
    lv_obj_t* hist_pane = pm_app_layout_pane(&L, hist_w, NULL);
    _build_histogram(hist_pane);

    // Action bar
    pm_app_layout_action(&L, "CLEAR",  PM_LAYOUT_COL_WARN,   _act_clear);
    pm_app_layout_action(&L, "EXPORT", PM_LAYOUT_COL_ACCENT, _act_export);
    pm_app_layout_action(&L, "FILTER", PM_LAYOUT_COL_PURPLE, _act_filter);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Render: sort, update labels, refresh histogram ───────
static int _cmp_rssi(const void* a, const void* b) {
    return ((const ap_t*)b)->rssi - ((const ap_t*)a)->rssi;
}

static void _render(void) {
    if (!s_built || !s_ap_list) return;

    // Sort the data layer by RSSI desc. Note: this also reorders
    // the underlying storage. That's fine — we look APs up by
    // BSSID on every input event.
    qsort(s_aps, s_count, sizeof(ap_t), _cmp_rssi);

    // Walk filtered APs and bind to UI rows in order.
    int  visible      = 0;
    int  ch_counts[HIST_CHANNELS] = {0};
    int  best_rssi    = 0;
    int  open_count   = 0;
    int  sec_count    = 0;
    int  hidden_count = 0;
    bool best_set     = false;

    for (int i = 0; i < s_count; i++) {
        const ap_t* a = &s_aps[i];

        // Tally for stats — always against the unfiltered set.
        if (a->channel >= 1 && a->channel < HIST_CHANNELS) {
            ch_counts[a->channel]++;
        }
        if (!best_set || a->rssi > best_rssi) {
            best_rssi = a->rssi;
            best_set  = true;
        }
        if (_enc_is_open(a->enc)) {
            open_count++;
        } else if (_enc_is_secured(a->enc)) {
            sec_count++;
        }
        if (!a->ssid[0]) hidden_count++;

        // Filter for the visible list.
        if (!_filter_accepts(a)) continue;
        if (visible >= MAX_VISIBLE) continue;

        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_ap_list);
            s_rows_created++;
        }
        ap_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);

        char b[12];
        snprintf(b, sizeof(b), "%d", a->channel);
        lv_label_set_text(r->ch_lbl, b);

        lv_bar_set_value(r->rssi_bar, _rssi_pct(a->rssi), LV_ANIM_OFF);
        lv_color_t rc = _rssi_color(a->rssi);
        lv_obj_set_style_bg_color(r->rssi_bar, rc, LV_PART_INDICATOR);

        char rb[12];
        snprintf(rb, sizeof(rb), "%d dBm", a->rssi);
        lv_label_set_text(r->rssi_lbl, rb);

        const char* enc_short = _enc_short(a->enc);
        lv_color_t  enc_col   = _enc_color(a->enc);
        lv_label_set_text(r->enc_lbl, enc_short);
        lv_obj_set_style_text_color(r->enc_lbl, enc_col, 0);
        lv_obj_set_style_border_color(r->enc_chip, enc_col, 0);
        lv_obj_set_style_bg_color(r->enc_chip, enc_col, 0);

        lv_label_set_text(r->bssid_lbl, a->bssid);
        lv_label_set_text(r->ssid_lbl,
                           a->ssid[0] ? a->ssid : "(hidden)");
        lv_obj_set_style_text_color(r->ssid_lbl,
            a->ssid[0] ? PM_LAYOUT_COL_FG_BR : PM_LAYOUT_COL_DIM, 0);

        char hb[8];
        snprintf(hb, sizeof(hb), "%d", a->hits);
        lv_label_set_text(r->hits_lbl, hb);

        visible++;
    }

    // Hide leftover row widgets (filter narrowed the set).
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    // Stats updates
    if (s_stat_count) {
        char b[12]; snprintf(b, sizeof(b), "%d", s_count);
        lv_label_set_text(s_stat_count, b);
    }
    int active_ch = 0;
    for (int c = 1; c < HIST_CHANNELS; c++) if (ch_counts[c] > 0) active_ch++;
    if (s_stat_channels) {
        char b[12]; snprintf(b, sizeof(b), "%d", active_ch);
        lv_label_set_text(s_stat_channels, b);
    }
    if (s_stat_best) {
        char b[12];
        if (best_set) snprintf(b, sizeof(b), "%d", best_rssi);
        else          snprintf(b, sizeof(b), "—");
        lv_label_set_text(s_stat_best, b);
    }
    if (s_stat_open) {
        char b[12]; snprintf(b, sizeof(b), "%d", open_count);
        lv_label_set_text(s_stat_open, b);
    }
    if (s_stat_secured) {
        char b[12]; snprintf(b, sizeof(b), "%d", sec_count);
        lv_label_set_text(s_stat_secured, b);
    }
    if (s_stat_hidden) {
        char b[12]; snprintf(b, sizeof(b), "%d", hidden_count);
        lv_label_set_text(s_stat_hidden, b);
    }

    // Histogram: bar height = (count / max_count) * pane height.
    int max_count = 1;
    for (int c = 1; c < HIST_CHANNELS; c++) {
        if (ch_counts[c] > max_count) max_count = ch_counts[c];
    }
    int pane_h = s_hist_box ? lv_obj_get_content_height(s_hist_box) : 80;
    if (pane_h < 40) pane_h = 80;
    int avail_h = pane_h - 18;   // leave room for the channel number label
    if (avail_h < 8) avail_h = 8;
    for (int c = 1; c < HIST_CHANNELS; c++) {
        int h = (ch_counts[c] * avail_h) / max_count;
        if (h < 2) h = 2;
        if (s_hist_bars[c]) {
            lv_obj_set_height(s_hist_bars[c], h);
            lv_color_t col = ch_counts[c] == 0 ? PM_LAYOUT_COL_DIM
                            : ch_counts[c] >= 3 ? PM_LAYOUT_COL_WARN
                            : PM_LAYOUT_COL_ACCENT;
            lv_obj_set_style_bg_color(s_hist_bars[c], col, 0);
        }
    }

    // Status chip text — "READY" vs "TRACKING N"
    if (s_chip_status) {
        if (s_count == 0) {
            lv_label_set_text(s_chip_status, "READY");
        } else {
            char b[24]; snprintf(b, sizeof(b), "TRACKING %d", s_count);
            lv_label_set_text(s_chip_status, b);
        }
    }
}

// ── App lifecycle ────────────────────────────────────────
static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter (tracking %d APs)", s_count);
    _render();
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now;
    _render();
}

static const pm_app_t _APP = {
    .id           = "beacon",
    .display_name = "BEACON",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_beacon(void) { return &_APP; }
