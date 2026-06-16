// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_wifi.c — WiFi scan + connect manager
//
//  Phase 19 redesign:
//
//  Layout matches the canonical pisces-moon-linux wifi_app.html
//  HTML mockup. Three columns with a stats row above:
//
//    [320/360 LEFT  ] [   CENTER (flex)  ] [260/320 RIGHT ]
//    NETWORKS list    Connection status   Network detail
//                     + 4 action buttons  Saved networks
//                     + RSSI history      Password input
//                     + channel use       + Connect button
//                     + security stats
//
//  The scan / connect / keyring / autoconnect logic is the
//  same as the Phase 18 implementation. Only the screen-build
//  layer changed.
//
//  Radio path:
//    All esp_wifi_* calls go through ESP-Hosted to the C6.
//    The wifi_scan SPI Treaty owner string is "wifi_app".
//    Saved credentials live in pm_nosql category "wifi_keyring",
//    one entry per SSID, value is a tiny JSON blob.
// ============================================================

#include "pm_app_wifi.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_c6_bridge.h"
#include "pm_radio_host.h"
#include "pm_nosql.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_WIFI";

#define WIFI_SCAN_OWNER  "wifi_app"
#define MAX_NETWORKS     64
#define SSID_LEN         33
#define BSSID_LEN        18
#define ENC_LEN          12
#define RSSI_HISTORY_LEN 30
#define MAX_VISIBLE_ROWS 24
#define MAX_SAVED_VIS    8
#define PASSWORD_LEN     64

// ── Model ───────────────────────────────────────────────────
typedef struct {
    char ssid[SSID_LEN];
    char bssid[BSSID_LEN];
    int  rssi;
    int  channel;
    char enc[ENC_LEN];
} pm_net_t;

static pm_net_t s_nets[MAX_NETWORKS];
static int      s_net_count   = 0;
static int      s_selected    = 0;
static bool     s_scanning    = false;
static bool     s_connected   = false;
static char     s_connected_ssid[SSID_LEN] = "";
static char     s_connected_ip[20]         = "";
static char     s_status_note[96]          = "";
static bool     s_wifi_evt_registered      = false;
static bool     s_dirty                    = false;

// Per-AP RSSI history sample buffer, indexed by selected SSID.
// Reset whenever the selection changes.
static int  s_rssi_hist[RSSI_HISTORY_LEN];
static int  s_rssi_hist_count = 0;
static char s_rssi_hist_ssid[SSID_LEN] = "";

// ── UI handles ──────────────────────────────────────────────
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_chip_state    = NULL;
static lv_obj_t* s_chip_source   = NULL;
static lv_obj_t* s_stat_total    = NULL;
static lv_obj_t* s_stat_open     = NULL;
static lv_obj_t* s_stat_secured  = NULL;
static lv_obj_t* s_stat_strong   = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_list_header   = NULL;
static lv_obj_t* s_conn_card     = NULL;
static lv_obj_t* s_conn_ssid_lbl = NULL;
static lv_obj_t* s_conn_ip_lbl   = NULL;
static lv_obj_t* s_rssi_chart    = NULL;
static lv_chart_series_t* s_rssi_series = NULL;
static lv_obj_t* s_chan_box      = NULL;
static lv_obj_t* s_chan_bars[14] = {0};
static lv_obj_t* s_sec_dist_lbl  = NULL;
static lv_obj_t* s_detail_lbl    = NULL;
static lv_obj_t* s_saved_box     = NULL;
static lv_obj_t* s_pass_input    = NULL;
static bool      s_built         = false;

// Per-row UI cache so we recycle instead of clean-rebuild.
typedef struct {
    lv_obj_t* row;
    lv_obj_t* bars[4];
    lv_obj_t* ssid_lbl;
    lv_obj_t* meta_lbl;
    lv_obj_t* sec_lbl;
} net_row_ui_t;

static net_row_ui_t s_rows[MAX_VISIBLE_ROWS];
static int          s_rows_created = 0;

// Forward
static void _render(void);
static void _build_screen(void);

// ── Helpers ─────────────────────────────────────────────────
static const char* _auth_to_str(wifi_auth_mode_t auth) {
    switch (auth) {
    case WIFI_AUTH_OPEN:           return "OPEN";
    case WIFI_AUTH_WEP:            return "WEP";
    case WIFI_AUTH_WPA_PSK:        return "WPA";
    case WIFI_AUTH_WPA2_PSK:       return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:       return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/3";
    default:                       return "?";
    }
}

static lv_color_t _sec_color(const char* enc) {
    if (!enc || !enc[0])          return PM_LAYOUT_COL_FG_DIM;
    if (strstr(enc, "OPEN"))      return PM_LAYOUT_COL_ERR;
    if (strstr(enc, "WEP"))       return PM_LAYOUT_COL_ERR;
    if (strstr(enc, "WPA3"))      return PM_LAYOUT_COL_ACCENT;
    if (strstr(enc, "WPA2"))      return PM_LAYOUT_COL_OK;
    if (strstr(enc, "WPA"))       return PM_LAYOUT_COL_WARN;
    return PM_LAYOUT_COL_FG_DIM;
}

// Map RSSI (dBm) to a 0..4 signal-bars value.
// -50 dBm and stronger = 4 bars, -100 dBm = 0 bars.
static int _rssi_to_bars(int rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    if (rssi >= -80) return 1;
    return 0;
}

// ── Bridge callbacks ────────────────────────────────────────
void pm_app_wifi_on_scan_result(const char* ssid, const char* bssid,
                                  int rssi, int channel, const char* enc) {
    if (s_net_count >= MAX_NETWORKS) return;
    pm_net_t* n = &s_nets[s_net_count++];
    strncpy(n->ssid,  ssid  ? ssid  : "", SSID_LEN  - 1);
    strncpy(n->bssid, bssid ? bssid : "", BSSID_LEN - 1);
    strncpy(n->enc,   enc   ? enc   : "?",  ENC_LEN - 1);
    n->ssid [SSID_LEN  - 1] = 0;
    n->bssid[BSSID_LEN - 1] = 0;
    n->enc  [ENC_LEN   - 1] = 0;
    n->rssi    = rssi;
    n->channel = channel;
    s_dirty = true;
}

void pm_app_wifi_on_scan_done(int total) {
    s_scanning = false;
    snprintf(s_status_note, sizeof(s_status_note),
             "scan done: %d returned / %d recorded", total, s_net_count);
    pm_log_i(TAG, "%s", s_status_note);
    s_dirty = true;
}

void pm_app_wifi_on_connected(const char* ssid, const char* ip) {
    s_connected = true;
    strncpy(s_connected_ssid, ssid ? ssid : "", sizeof(s_connected_ssid) - 1);
    strncpy(s_connected_ip,   ip   ? ip   : "", sizeof(s_connected_ip)   - 1);
    s_dirty = true;
    pm_log_i(TAG, "connected: %s @ %s", s_connected_ssid, s_connected_ip);
}

void pm_app_wifi_on_disconnected(void) {
    s_connected = false;
    s_connected_ssid[0] = 0;
    s_connected_ip[0]   = 0;
    s_dirty = true;
    pm_log_i(TAG, "disconnected");
}

static void _wifi_scan_done_handler(void* arg, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
    (void)arg; (void)event_data;
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;
    if (!pm_wifi_scan_is_owner(WIFI_SCAN_OWNER)) return;

    uint16_t ap_count = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        s_scanning = false;
        pm_wifi_scan_give(WIFI_SCAN_OWNER);
        snprintf(s_status_note, sizeof(s_status_note),
                 "scan count failed: %s", esp_err_to_name(err));
        s_dirty = true;
        return;
    }
    if (ap_count > MAX_NETWORKS) ap_count = MAX_NETWORKS;
    wifi_ap_record_t* records = NULL;
    uint16_t requested = ap_count;
    if (requested > 0) {
        records = (wifi_ap_record_t*)calloc(requested, sizeof(wifi_ap_record_t));
        if (!records) {
            s_scanning = false;
            esp_wifi_clear_ap_list();
            pm_wifi_scan_give(WIFI_SCAN_OWNER);
            return;
        }
        err = esp_wifi_scan_get_ap_records(&requested, records);
        if (err != ESP_OK) {
            s_scanning = false;
            free(records);
            esp_wifi_clear_ap_list();
            pm_wifi_scan_give(WIFI_SCAN_OWNER);
            return;
        }
    }
    s_net_count = 0;
    for (uint16_t i = 0; i < requested; i++) {
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 records[i].bssid[0], records[i].bssid[1],
                 records[i].bssid[2], records[i].bssid[3],
                 records[i].bssid[4], records[i].bssid[5]);
        pm_app_wifi_on_scan_result((const char*)records[i].ssid,
                                   bssid, records[i].rssi,
                                   records[i].primary,
                                   _auth_to_str(records[i].authmode));
    }
    free(records);
    esp_wifi_clear_ap_list();
    pm_app_wifi_on_scan_done((int)requested);
    pm_wifi_scan_give(WIFI_SCAN_OWNER);
}

static void _register_wifi_events(void) {
    if (s_wifi_evt_registered) return;
    esp_err_t err = esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
        _wifi_scan_done_handler, NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_wifi_evt_registered = true;
    } else {
        snprintf(s_status_note, sizeof(s_status_note),
                 "event register failed: %s", esp_err_to_name(err));
    }
}

// ── Keyring ─────────────────────────────────────────────────
static void _save_keyring(const char* ssid, const char* pass) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ssid\":\"%s\",\"pass\":\"%s\"}", ssid, pass);
    pm_nosql_write("wifi_keyring", ssid, buf, (size_t)n);
}

static const char* _keyring_lookup(const char* ssid, char* out_pass, size_t cap) {
    char buf[256];
    size_t got = pm_nosql_read("wifi_keyring", ssid, buf, sizeof(buf));
    if (got == 0) return NULL;
    cJSON* root = cJSON_Parse(buf);
    if (!root) return NULL;
    const cJSON* p = cJSON_GetObjectItem(root, "pass");
    if (!cJSON_IsString(p) || !p->valuestring) { cJSON_Delete(root); return NULL; }
    strncpy(out_pass, p->valuestring, cap - 1);
    out_pass[cap - 1] = 0;
    cJSON_Delete(root);
    return out_pass;
}

static void _forget_keyring(const char* ssid) {
    pm_nosql_delete("wifi_keyring", ssid);
}

// ── Actions ─────────────────────────────────────────────────
static void _do_scan(void) {
    if (!pm_wifi_scan_take(WIFI_SCAN_OWNER, 0)) {
        snprintf(s_status_note, sizeof(s_status_note),
                 "scanner busy: %s", pm_wifi_scan_owner());
        s_dirty = true;
        return;
    }
    s_net_count = 0;
    s_scanning  = true;
    s_status_note[0] = 0;
    _register_wifi_events();
    s_dirty = true;

    wifi_scan_config_t cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 250 } },
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        s_scanning = false;
        pm_wifi_scan_give(WIFI_SCAN_OWNER);
        snprintf(s_status_note, sizeof(s_status_note),
                 "scan_start failed: %s", esp_err_to_name(err));
    }
}

static void _do_connect_selected(void) {
    if (s_selected < 0 || s_selected >= s_net_count) return;
    const pm_net_t* n = &s_nets[s_selected];
    char saved_pass[PASSWORD_LEN];

    // Prefer saved credentials; fall back to whatever's in the
    // password textbox if the user typed one.
    const char* pass = _keyring_lookup(n->ssid, saved_pass, sizeof(saved_pass));
    char typed[PASSWORD_LEN] = "";
    if (!pass && s_pass_input) {
        const char* tx = lv_textarea_get_text(s_pass_input);
        if (tx && tx[0]) {
            strncpy(typed, tx, sizeof(typed) - 1);
            pass = typed;
        }
    }

    wifi_config_t cfg = {0};
    strncpy((char*)cfg.sta.ssid, n->ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass) strncpy((char*)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        snprintf(s_status_note, sizeof(s_status_note),
                 "set_config failed: %s", esp_err_to_name(err));
        s_dirty = true;
        return;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        snprintf(s_status_note, sizeof(s_status_note),
                 "connect failed: %s", esp_err_to_name(err));
    } else {
        if (pass && pass[0]) _save_keyring(n->ssid, pass);
        snprintf(s_status_note, sizeof(s_status_note),
                 "connecting to %s...", n->ssid);
    }
    s_dirty = true;
}

static void _do_disconnect(void) {
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        snprintf(s_status_note, sizeof(s_status_note),
                 "disconnect failed: %s", esp_err_to_name(err));
    } else {
        snprintf(s_status_note, sizeof(s_status_note), "disconnecting...");
    }
    s_dirty = true;
}

static void _do_forget_selected(void) {
    if (s_selected < 0 || s_selected >= s_net_count) return;
    _forget_keyring(s_nets[s_selected].ssid);
    snprintf(s_status_note, sizeof(s_status_note),
             "forgot %s", s_nets[s_selected].ssid);
    s_dirty = true;
}

// LVGL event shims
static void _ev_scan(lv_event_t* e)       { (void)e; _do_scan();           }
static void _ev_connect(lv_event_t* e)    { (void)e; _do_connect_selected(); }
static void _ev_disconnect(lv_event_t* e) { (void)e; _do_disconnect();     }
static void _ev_forget(lv_event_t* e)     { (void)e; _do_forget_selected(); }

// ── Row click ───────────────────────────────────────────────
static void _row_clicked(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_net_count) return;
    if (s_selected != idx) {
        s_selected = idx;
        // New selection — clear RSSI history; first sample comes
        // on the next scan that touches this AP.
        s_rssi_hist_count = 0;
        strncpy(s_rssi_hist_ssid, s_nets[idx].ssid, sizeof(s_rssi_hist_ssid) - 1);
        s_rssi_hist_ssid[sizeof(s_rssi_hist_ssid) - 1] = 0;
    }
    s_dirty = true;
}

// ── Row UI ──────────────────────────────────────────────────
//
// Each row is a 44px horizontal strip:
//   [signal bars 18px] [ssid + meta col, flex grow] [sec badge 50px]
//
// The bars are four lv_obj rectangles of increasing height,
// the "lit" ones tinted accent3 and the rest panel-border.
static void _build_row(int slot, lv_obj_t* parent) {
    net_row_ui_t* r = &s_rows[slot];
    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 46);
    lv_obj_set_style_bg_color(r->row, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r->row, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_ver(r->row, 6, 0);
    lv_obj_set_style_pad_column(r->row, 10, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->row, _row_clicked, LV_EVENT_CLICKED,
                          (void*)(intptr_t)slot);

    // Signal bars container — 4 vertical rectangles
    lv_obj_t* bars = lv_obj_create(r->row);
    lv_obj_remove_style_all(bars);
    lv_obj_set_size(bars, 24, 22);
    lv_obj_set_style_pad_column(bars, 2, 0);
    lv_obj_set_layout(bars, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_clear_flag(bars, LV_OBJ_FLAG_SCROLLABLE);
    static const int bar_heights[4] = {6, 11, 16, 22};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = lv_obj_create(bars);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 4, bar_heights[i]);
        lv_obj_set_style_bg_color(b, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        r->bars[i] = b;
    }

    // SSID + meta column
    lv_obj_t* col = lv_obj_create(r->row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    r->ssid_lbl = lv_label_create(col);
    lv_label_set_text(r->ssid_lbl, "—");
    lv_label_set_long_mode(r->ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->ssid_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->ssid_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->ssid_lbl, PM_LAYOUT_COL_FG_BR, 0);

    r->meta_lbl = lv_label_create(col);
    lv_label_set_text(r->meta_lbl, "");
    lv_label_set_long_mode(r->meta_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->meta_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->meta_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->meta_lbl, PM_LAYOUT_COL_FG_DIM, 0);

    // Security badge
    r->sec_lbl = lv_label_create(r->row);
    lv_label_set_text(r->sec_lbl, "");
    lv_obj_set_style_text_font(r->sec_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->sec_lbl, PM_LAYOUT_COL_FG_DIM, 0);
    lv_obj_set_style_text_letter_space(r->sec_lbl, 1, 0);
}

static void _render_row(int slot, int idx, bool selected) {
    net_row_ui_t* r = &s_rows[slot];
    const pm_net_t* n = &s_nets[idx];

    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);

    // Selection accent: 2px left border in accent color + tinted bg.
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

    int bars = _rssi_to_bars(n->rssi);
    lv_color_t bar_col =
        (bars >= 3) ? PM_LAYOUT_COL_OK :
        (bars >= 2) ? PM_LAYOUT_COL_WARN :
                      PM_LAYOUT_COL_ERR;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(r->bars[i],
            (i < bars) ? bar_col : PM_LAYOUT_COL_BORDER, 0);
    }

    lv_label_set_text(r->ssid_lbl,
        n->ssid[0] ? n->ssid : "(hidden)");

    char meta[40];
    snprintf(meta, sizeof(meta), "ch %d  %d dBm", n->channel, n->rssi);
    lv_label_set_text(r->meta_lbl, meta);

    lv_label_set_text(r->sec_lbl, n->enc);
    lv_obj_set_style_text_color(r->sec_lbl, _sec_color(n->enc), 0);
}

// ── Right pane: saved networks ──────────────────────────────
static void _render_saved(void) {
    if (!s_saved_box) return;
    lv_obj_clean(s_saved_box);

    // pm_nosql_list returns SSIDs space-separated into a flat buffer.
    char ids[MAX_SAVED_VIS * 36];
    int count = pm_nosql_list("wifi_keyring", ids, MAX_SAVED_VIS, 36);
    if (count <= 0) {
        lv_obj_t* l = lv_label_create(s_saved_box);
        lv_label_set_text(l, "no saved networks");
        lv_obj_set_style_text_color(l, PM_LAYOUT_COL_FG_DIM, 0);
        lv_obj_set_style_text_font(l, PM_LAYOUT_FONT_LABEL, 0);
        return;
    }
    for (int i = 0; i < count; i++) {
        const char* ssid = &ids[i * 36];
        if (!ssid[0]) break;
        lv_obj_t* l = lv_label_create(s_saved_box);
        lv_label_set_text(l, ssid);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(100));
        lv_obj_set_style_text_color(l, PM_LAYOUT_COL_ACCENT, 0);
        lv_obj_set_style_text_font(l, PM_LAYOUT_FONT_LABEL, 0);
    }
}

// ── Top-level render ────────────────────────────────────────
static void _render(void) {
    if (!s_built) return;

    // Chips
    if (s_chip_state) {
        if (s_scanning) {
            lv_label_set_text(s_chip_state, "SCANNING");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_ACCENT, 0);
        } else if (s_connected) {
            lv_label_set_text(s_chip_state, "CONNECTED");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_OK, 0);
        } else {
            lv_label_set_text(s_chip_state, "IDLE");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_FG_DIM, 0);
        }
    }

    // Stats
    int open_n = 0, sec_n = 0, strongest = -127;
    char strongest_ssid[SSID_LEN] = "—";
    int chan_hist[14] = {0};
    int sec_breakdown[5] = {0};   // OPEN, WEP, WPA, WPA2, WPA3
    for (int i = 0; i < s_net_count; i++) {
        const pm_net_t* n = &s_nets[i];
        bool is_open = (strstr(n->enc, "OPEN") != NULL);
        if (is_open) open_n++; else sec_n++;
        if (n->rssi > strongest) {
            strongest = n->rssi;
            strncpy(strongest_ssid, n->ssid[0] ? n->ssid : "(hidden)",
                     sizeof(strongest_ssid) - 1);
        }
        if (n->channel >= 1 && n->channel <= 14) {
            chan_hist[n->channel - 1]++;
        }
        if      (strstr(n->enc, "OPEN")) sec_breakdown[0]++;
        else if (strstr(n->enc, "WEP"))  sec_breakdown[1]++;
        else if (strstr(n->enc, "WPA3")) sec_breakdown[4]++;
        else if (strstr(n->enc, "WPA2")) sec_breakdown[3]++;
        else if (strstr(n->enc, "WPA"))  sec_breakdown[2]++;
    }
    char b[24];
    snprintf(b, sizeof(b), "%d", s_net_count);
    if (s_stat_total) lv_label_set_text(s_stat_total, b);
    snprintf(b, sizeof(b), "%d", open_n);
    if (s_stat_open) {
        lv_label_set_text(s_stat_open, b);
        pm_app_layout_stat_color(s_stat_open,
            open_n > 0 ? PM_LAYOUT_COL_ERR : PM_LAYOUT_COL_FG_BR);
    }
    snprintf(b, sizeof(b), "%d", sec_n);
    if (s_stat_secured) {
        lv_label_set_text(s_stat_secured, b);
        pm_app_layout_stat_color(s_stat_secured, PM_LAYOUT_COL_OK);
    }
    if (s_stat_strong) {
        char sb[24];
        if (s_net_count > 0) snprintf(sb, sizeof(sb), "%d", strongest);
        else strcpy(sb, "—");
        lv_label_set_text(s_stat_strong, sb);
        pm_app_layout_stat_color(s_stat_strong, PM_LAYOUT_COL_GOLD);
    }

    // List header count
    if (s_list_header) {
        char hb[24];
        snprintf(hb, sizeof(hb), "%d", s_net_count);
        lv_label_set_text(s_list_header, hb);
    }

    // Rows
    int visible = 0;
    for (int i = 0; i < s_net_count && i < MAX_VISIBLE_ROWS; i++) {
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        _render_row(visible, i, i == s_selected);
        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    // Connection status
    if (s_conn_ssid_lbl) {
        lv_label_set_text(s_conn_ssid_lbl,
            s_connected ? s_connected_ssid : "NOT CONNECTED");
        lv_obj_set_style_text_color(s_conn_ssid_lbl,
            s_connected ? PM_LAYOUT_COL_OK : PM_LAYOUT_COL_FG_DIM, 0);
    }
    if (s_conn_ip_lbl) {
        if (s_connected) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", s_connected_ip);
            lv_label_set_text(s_conn_ip_lbl, buf);
        } else if (s_status_note[0]) {
            lv_label_set_text(s_conn_ip_lbl, s_status_note);
        } else {
            lv_label_set_text(s_conn_ip_lbl,
                "Select a network and tap CONNECT");
        }
    }

    // RSSI chart — push the selected SSID's most recent sample
    // each scan cycle so the user sees their selected AP's
    // signal history evolve.
    if (s_selected >= 0 && s_selected < s_net_count) {
        const pm_net_t* sel = &s_nets[s_selected];
        if (strcmp(sel->ssid, s_rssi_hist_ssid) != 0) {
            strncpy(s_rssi_hist_ssid, sel->ssid,
                     sizeof(s_rssi_hist_ssid) - 1);
            s_rssi_hist_ssid[sizeof(s_rssi_hist_ssid) - 1] = 0;
            s_rssi_hist_count = 0;
        }
        if (s_rssi_hist_count < RSSI_HISTORY_LEN) {
            s_rssi_hist[s_rssi_hist_count++] = sel->rssi;
        } else {
            memmove(&s_rssi_hist[0], &s_rssi_hist[1],
                     sizeof(int) * (RSSI_HISTORY_LEN - 1));
            s_rssi_hist[RSSI_HISTORY_LEN - 1] = sel->rssi;
        }
        if (s_rssi_chart && s_rssi_series) {
            for (int i = 0; i < s_rssi_hist_count; i++) {
                lv_chart_set_value_by_id(s_rssi_chart, s_rssi_series, i,
                                          s_rssi_hist[i]);
            }
            lv_chart_refresh(s_rssi_chart);
        }
    }

    // Channel histogram — bar heights proportional to channel count
    int chan_max = 1;
    for (int i = 0; i < 14; i++) if (chan_hist[i] > chan_max) chan_max = chan_hist[i];
    for (int i = 0; i < 14; i++) {
        if (!s_chan_bars[i]) continue;
        int pct = chan_hist[i] * 100 / chan_max;
        if (pct < 4 && chan_hist[i] > 0) pct = 4;
        lv_obj_set_height(s_chan_bars[i], pct + 2);
        lv_color_t col = chan_hist[i] == 0 ? PM_LAYOUT_COL_BORDER :
                         chan_hist[i] >= 4 ? PM_LAYOUT_COL_ERR :
                         chan_hist[i] >= 2 ? PM_LAYOUT_COL_WARN :
                                              PM_LAYOUT_COL_OK;
        lv_obj_set_style_bg_color(s_chan_bars[i], col, 0);
    }

    // Security distribution label
    if (s_sec_dist_lbl) {
        char sb[128];
        snprintf(sb, sizeof(sb),
            "#ff5577 OPEN %d#  #ffe066 WPA %d#  #4dffa6 WPA2 %d#  #4dd9ff WPA3 %d#",
            sec_breakdown[0], sec_breakdown[2],
            sec_breakdown[3], sec_breakdown[4]);
        lv_label_set_text(s_sec_dist_lbl, sb);
        lv_label_set_recolor(s_sec_dist_lbl, true);
    }

    // Detail pane
    if (s_detail_lbl && s_selected >= 0 && s_selected < s_net_count) {
        const pm_net_t* n = &s_nets[s_selected];
        char buf[256];
        snprintf(buf, sizeof(buf),
            "SSID:    %s\n"
            "BSSID:   %s\n"
            "RSSI:    %d dBm  (%d bars)\n"
            "Channel: %d\n"
            "Auth:    %s",
            n->ssid[0] ? n->ssid : "(hidden)",
            n->bssid, n->rssi, _rssi_to_bars(n->rssi),
            n->channel, n->enc);
        lv_label_set_text(s_detail_lbl, buf);
    } else if (s_detail_lbl) {
        lv_label_set_text(s_detail_lbl, "Select a network on the left.");
    }

    _render_saved();
}

// ── Screen build ────────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "WIFI MANAGER");

    s_chip_state  = pm_app_layout_chip(&L, "IDLE",    PM_LAYOUT_COL_FG_DIM);
    s_chip_source = pm_app_layout_chip(&L, "C6 HOST", PM_LAYOUT_COL_ACCENT);

    pm_app_layout_stats_row(&L, 4);
    s_stat_total   = pm_app_layout_stat(&L, "NETWORKS",  "0");
    s_stat_open    = pm_app_layout_stat(&L, "OPEN",      "0");
    s_stat_secured = pm_app_layout_stat(&L, "SECURED",   "0");
    s_stat_strong  = pm_app_layout_stat(&L, "STRONGEST", "—");

    pm_app_layout_content(&L);

#if PM_BOARD_LCD_H_RES <= 800
    int left_w  = 280;
    int right_w = 240;
#else
    int left_w  = 360;
    int right_w = 300;
#endif

    // ── LEFT pane — network list ──
    lv_obj_t* left = pm_app_layout_pane(&L, left_w, NULL);
    {
        // Header strip with count
        lv_obj_t* sh = pm_app_layout_section_header(left, "NETWORKS", "0");
        // section_header puts the accent count as the second child;
        // grab it so we can update it on render.
        if (sh && lv_obj_get_child_count(sh) > 1) {
            s_list_header = lv_obj_get_child(sh, 1);
        }

        s_list_box = lv_obj_create(left);
        lv_obj_remove_style_all(s_list_box);
        lv_obj_set_width(s_list_box, LV_PCT(100));
        lv_obj_set_flex_grow(s_list_box, 1);
        lv_obj_set_style_bg_opa(s_list_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(s_list_box, 0, 0);
        lv_obj_set_layout(s_list_box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_list_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_list_box, 0, 0);
        lv_obj_add_flag(s_list_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_list_box, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_list_box, LV_SCROLLBAR_MODE_AUTO);
    }

    // ── CENTER pane — connection + charts ──
    lv_obj_t* center = pm_app_layout_pane(&L, 0, NULL);
    {
        // Connection status card
        s_conn_card = lv_obj_create(center);
        lv_obj_remove_style_all(s_conn_card);
        lv_obj_set_width(s_conn_card, LV_PCT(100));
        lv_obj_set_height(s_conn_card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(s_conn_card, PM_LAYOUT_COL_BG2, 0);
        lv_obj_set_style_bg_opa(s_conn_card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_conn_card, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(s_conn_card, 1, 0);
        lv_obj_set_style_border_side(s_conn_card, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_all(s_conn_card, 14, 0);
        lv_obj_set_style_pad_row(s_conn_card, 6, 0);
        lv_obj_set_layout(s_conn_card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_conn_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(s_conn_card, LV_OBJ_FLAG_SCROLLABLE);

        s_conn_ssid_lbl = lv_label_create(s_conn_card);
        lv_label_set_text(s_conn_ssid_lbl, "NOT CONNECTED");
        lv_obj_set_style_text_font(s_conn_ssid_lbl, PM_LAYOUT_FONT_TITLE, 0);
        lv_obj_set_style_text_color(s_conn_ssid_lbl, PM_LAYOUT_COL_FG_DIM, 0);
        lv_obj_set_style_text_letter_space(s_conn_ssid_lbl, 2, 0);

        s_conn_ip_lbl = lv_label_create(s_conn_card);
        lv_label_set_text(s_conn_ip_lbl,
            "Select a network and tap CONNECT");
        lv_label_set_long_mode(s_conn_ip_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_conn_ip_lbl, LV_PCT(100));
        lv_obj_set_style_text_font(s_conn_ip_lbl, PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(s_conn_ip_lbl, PM_LAYOUT_COL_FG_DIM, 0);

        // RSSI history chart
        lv_obj_t* csec1 = pm_app_layout_chart_section(center,
            "SIGNAL HISTORY — SELECTED");
        s_rssi_chart = lv_chart_create(csec1);
        lv_obj_set_size(s_rssi_chart, LV_PCT(100), 80);
        lv_chart_set_type(s_rssi_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(s_rssi_chart, RSSI_HISTORY_LEN);
        lv_chart_set_range(s_rssi_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
        lv_obj_set_style_bg_color(s_rssi_chart, PM_LAYOUT_COL_BG, 0);
        lv_obj_set_style_bg_opa(s_rssi_chart, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_rssi_chart, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(s_rssi_chart, 1, 0);
        lv_obj_set_style_line_width(s_rssi_chart, 0, LV_PART_TICKS);
        lv_chart_set_div_line_count(s_rssi_chart, 3, 0);
        s_rssi_series = lv_chart_add_series(s_rssi_chart,
            PM_LAYOUT_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_size(s_rssi_chart, 0, 0, LV_PART_INDICATOR);

        // Channel utilization
        lv_obj_t* csec2 = pm_app_layout_chart_section(center,
            "CHANNEL UTILIZATION (1–14)");
        s_chan_box = lv_obj_create(csec2);
        lv_obj_remove_style_all(s_chan_box);
        lv_obj_set_size(s_chan_box, LV_PCT(100), 70);
        lv_obj_set_style_bg_color(s_chan_box, PM_LAYOUT_COL_BG, 0);
        lv_obj_set_style_bg_opa(s_chan_box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_chan_box, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(s_chan_box, 1, 0);
        lv_obj_set_style_pad_all(s_chan_box, 8, 0);
        lv_obj_set_style_pad_column(s_chan_box, 4, 0);
        lv_obj_set_layout(s_chan_box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_chan_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_chan_box, LV_FLEX_ALIGN_SPACE_AROUND,
                              LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_clear_flag(s_chan_box, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 14; i++) {
            lv_obj_t* bar = lv_obj_create(s_chan_box);
            lv_obj_remove_style_all(bar);
            lv_obj_set_size(bar, 8, 4);
            lv_obj_set_style_bg_color(bar, PM_LAYOUT_COL_BORDER, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bar, 1, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            s_chan_bars[i] = bar;
        }

        // Security distribution
        lv_obj_t* csec3 = pm_app_layout_chart_section(center,
            "SECURITY DISTRIBUTION");
        s_sec_dist_lbl = lv_label_create(csec3);
        lv_label_set_text(s_sec_dist_lbl, "—");
        lv_label_set_long_mode(s_sec_dist_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_sec_dist_lbl, LV_PCT(100));
        lv_label_set_recolor(s_sec_dist_lbl, true);
        lv_obj_set_style_text_font(s_sec_dist_lbl, PM_LAYOUT_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s_sec_dist_lbl, PM_LAYOUT_COL_FG, 0);
        lv_obj_set_style_text_letter_space(s_sec_dist_lbl, 1, 0);
    }

    // ── RIGHT pane — detail + saved + password ──
    lv_obj_t* right = pm_app_layout_pane(&L, right_w, NULL);
    {
        pm_app_layout_section_header(right, "NETWORK DETAIL", NULL);
        s_detail_lbl = lv_label_create(right);
        lv_label_set_text(s_detail_lbl, "Select a network on the left.");
        lv_label_set_long_mode(s_detail_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_detail_lbl, LV_PCT(100));
        lv_obj_set_style_pad_all(s_detail_lbl, 12, 0);
        lv_obj_set_style_text_font(s_detail_lbl, PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(s_detail_lbl, PM_LAYOUT_COL_FG, 0);
        lv_obj_set_style_text_line_space(s_detail_lbl, 3, 0);

        pm_app_layout_section_header(right, "SAVED NETWORKS", NULL);
        s_saved_box = lv_obj_create(right);
        lv_obj_remove_style_all(s_saved_box);
        lv_obj_set_width(s_saved_box, LV_PCT(100));
        lv_obj_set_height(s_saved_box, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(s_saved_box, 12, 0);
        lv_obj_set_style_pad_row(s_saved_box, 4, 0);
        lv_obj_set_layout(s_saved_box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_saved_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(s_saved_box, LV_OBJ_FLAG_SCROLLABLE);

        pm_app_layout_section_header(right, "CONNECT", NULL);
        lv_obj_t* form = lv_obj_create(right);
        lv_obj_remove_style_all(form);
        lv_obj_set_width(form, LV_PCT(100));
        lv_obj_set_height(form, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(form, 12, 0);
        lv_obj_set_style_pad_row(form, 6, 0);
        lv_obj_set_layout(form, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* hint = lv_label_create(form);
        lv_label_set_text(hint, "Password (leave blank to use saved):");
        lv_obj_set_style_text_color(hint, PM_LAYOUT_COL_FG_DIM, 0);
        lv_obj_set_style_text_font(hint, PM_LAYOUT_FONT_LABEL, 0);

        s_pass_input = lv_textarea_create(form);
        lv_textarea_set_placeholder_text(s_pass_input, "WPA password");
        lv_textarea_set_password_mode(s_pass_input, true);
        lv_textarea_set_one_line(s_pass_input, true);
        lv_obj_set_width(s_pass_input, LV_PCT(100));
        lv_obj_set_style_bg_color(s_pass_input, PM_LAYOUT_COL_BG3, 0);
        lv_obj_set_style_text_color(s_pass_input, PM_LAYOUT_COL_FG_BR, 0);
        lv_obj_set_style_border_color(s_pass_input, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(s_pass_input, 1, 0);
    }

    pm_app_layout_action(&L, "SCAN",       PM_LAYOUT_COL_ACCENT, _ev_scan);
    pm_app_layout_action(&L, "CONNECT",    PM_LAYOUT_COL_OK,     _ev_connect);
    pm_app_layout_action(&L, "DISCONNECT", PM_LAYOUT_COL_WARN,   _ev_disconnect);
    pm_app_layout_action(&L, "FORGET",     PM_LAYOUT_COL_ERR,    _ev_forget);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ───────────────────────────────────────────────
static void _init(void) {
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    _register_wifi_events();
    pm_radio_host_wifi_scan_subscribe();
    _render();
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    s_last_render = now;
    if (s_dirty) {
        s_dirty = false;
        _render();
    }
}

static void _exit_(void) {
    if (s_scanning && pm_wifi_scan_is_owner(WIFI_SCAN_OWNER)) {
        s_scanning = false;
        esp_wifi_scan_stop();
        pm_wifi_scan_give(WIFI_SCAN_OWNER);
    }
    pm_radio_host_wifi_scan_unsubscribe();
}

static const pm_app_t _APP = {
    .id           = "wifi",
    .display_name = "WIFI",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_wifi(void) { return &_APP; }
