// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_wifi.c — WiFi scan + connect via C6 bridge
//
//  Scanned APs come in as wifi_scan_result events from the
//  C6. This app accumulates them, displays results, and
//  offers connect via saved keyring.
//
//  Keyring: pm_nosql category "wifi_keyring", id = SSID,
//  value = JSON {"ssid":"...","pass":"..."}.
//
//  C6 commands required (still pending in C6 firmware):
//    wifi_scan / wifi_connect / wifi_disconnect / wifi_status
//  C6 events required:
//    wifi_scan_result / wifi_scan_done / wifi_connected /
//    wifi_disconnected
// ============================================================

#include "pm_app_wifi.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "pm_nosql.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_WIFI";

#define MAX_NETWORKS 64
#define SSID_LEN     33
#define BSSID_LEN    18
#define ENC_LEN      8

typedef struct {
    char ssid[SSID_LEN];
    char bssid[BSSID_LEN];
    int  rssi;
    int  channel;
    char enc[ENC_LEN];
} pm_net_t;

static pm_net_t s_nets[MAX_NETWORKS];
static int      s_net_count   = 0;
static int      s_cursor      = 0;
static bool     s_scanning    = false;
static bool     s_connected   = false;
static char     s_connected_ssid[SSID_LEN] = "";
static char     s_connected_ip[20]         = "";
static char     s_status_note[96]          = "";
static bool     s_wifi_evt_registered      = false;

// LVGL
static lv_obj_t* s_wifi_screen = NULL;
static lv_obj_t* s_status_lbl  = NULL;
static lv_obj_t* s_list        = NULL;

static void _refresh_status(void);
static void _refresh_list(void);
static void _render(void);

static const char* _auth_to_str(wifi_auth_mode_t auth) {
    switch (auth) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
    }
}

// ─────────────────────────────────────────────
//  Bridge callbacks
// ─────────────────────────────────────────────
void pm_app_wifi_on_scan_result(const char* ssid, const char* bssid,
                                  int rssi, int channel, const char* enc) {
    if (s_net_count >= MAX_NETWORKS) return;
    pm_net_t* n = &s_nets[s_net_count++];
    strncpy(n->ssid,  ssid  ? ssid  : "", SSID_LEN  - 1);
    strncpy(n->bssid, bssid ? bssid : "", BSSID_LEN - 1);
    strncpy(n->enc,   enc   ? enc   : "?",  ENC_LEN - 1);
    n->ssid[SSID_LEN - 1] = 0;
    n->bssid[BSSID_LEN - 1] = 0;
    n->enc[ENC_LEN - 1] = 0;
    n->rssi    = rssi;
    n->channel = channel;
}

void pm_app_wifi_on_scan_done(int total) {
    s_scanning = false;
    snprintf(s_status_note, sizeof(s_status_note),
             "scan done: %d returned / %d recorded", total, s_net_count);
    pm_log_i(TAG, "scan done: %d networks (recorded %d)", total, s_net_count);
}

void pm_app_wifi_on_connected(const char* ssid, const char* ip) {
    s_connected = true;
    strncpy(s_connected_ssid, ssid ? ssid : "", sizeof(s_connected_ssid) - 1);
    strncpy(s_connected_ip,   ip   ? ip   : "", sizeof(s_connected_ip)   - 1);
    pm_log_i(TAG, "connected: %s @ %s", s_connected_ssid, s_connected_ip);
}

void pm_app_wifi_on_disconnected(void) {
    s_connected = false;
    s_connected_ssid[0] = 0;
    s_connected_ip[0]   = 0;
    pm_log_i(TAG, "disconnected");
}

static void _wifi_scan_done_handler(void* arg, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;

    uint16_t ap_count = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        s_scanning = false;
        snprintf(s_status_note, sizeof(s_status_note),
                 "scan count failed: %s", esp_err_to_name(err));
        pm_log_w(TAG, "%s", s_status_note);
        if (lvgl_port_lock(0)) {
            _render();
            lvgl_port_unlock();
        }
        return;
    }

    if (ap_count > MAX_NETWORKS) ap_count = MAX_NETWORKS;
    wifi_ap_record_t* records = NULL;
    uint16_t requested = ap_count;
    if (requested > 0) {
        records = (wifi_ap_record_t*)calloc(requested, sizeof(wifi_ap_record_t));
        if (!records) {
            s_scanning = false;
            snprintf(s_status_note, sizeof(s_status_note),
                     "scan alloc failed for %u APs", (unsigned)requested);
            pm_log_w(TAG, "%s", s_status_note);
            if (lvgl_port_lock(0)) {
                _render();
                lvgl_port_unlock();
            }
            return;
        }
        err = esp_wifi_scan_get_ap_records(&requested, records);
        if (err != ESP_OK) {
            s_scanning = false;
            free(records);
            snprintf(s_status_note, sizeof(s_status_note),
                     "scan read failed: %s", esp_err_to_name(err));
            pm_log_w(TAG, "%s", s_status_note);
            if (lvgl_port_lock(0)) {
                _render();
                lvgl_port_unlock();
            }
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
                                   bssid,
                                   records[i].rssi,
                                   records[i].primary,
                                   _auth_to_str(records[i].authmode));
    }
    free(records);
    esp_wifi_clear_ap_list();
    pm_app_wifi_on_scan_done((int)requested);

    if (lvgl_port_lock(0)) {
        _render();
        lvgl_port_unlock();
    }
}

static void _register_wifi_events(void) {
    if (s_wifi_evt_registered) return;
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                               _wifi_scan_done_handler, NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_wifi_evt_registered = true;
        return;
    }
    snprintf(s_status_note, sizeof(s_status_note),
             "event register failed: %s", esp_err_to_name(err));
    pm_log_w(TAG, "%s", s_status_note);
}

// ─────────────────────────────────────────────
//  Actions
// ─────────────────────────────────────────────
void pm_app_wifi_action_scan(void) {
    s_net_count = 0;
    s_scanning  = true;
    s_status_note[0] = 0;
    _register_wifi_events();
    _render();

    wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 250 } },
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        s_scanning = false;
        snprintf(s_status_note, sizeof(s_status_note),
                 "scan_start failed: %s", esp_err_to_name(err));
        pm_log_w(TAG, "%s", s_status_note);
        _render();
    }
}

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

void pm_app_wifi_connect(const char* ssid, const char* pass) {
    if (!ssid || !pass) return;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "{\"cmd\":\"wifi_connect\",\"ssid\":\"%s\",\"pass\":\"%s\"}",
             ssid, pass);
    pm_c6_cmd_send_raw(cmd);
    _save_keyring(ssid, pass);
}

void pm_app_wifi_connect_at_cursor(void) {
    if (s_cursor < 0 || s_cursor >= s_net_count) return;
    char pass[64];
    if (_keyring_lookup(s_nets[s_cursor].ssid, pass, sizeof(pass))) {
        pm_app_wifi_connect(s_nets[s_cursor].ssid, pass);
    } else {
        // TODO_LVGL: open password input modal — on submit call connect.
        pm_log_i(TAG, "no saved pass for '%s' — UI prompt needed",
                 s_nets[s_cursor].ssid);
    }
}

void pm_app_wifi_disconnect(void) {
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        snprintf(s_status_note, sizeof(s_status_note),
                 "disconnect failed: %s", esp_err_to_name(err));
        pm_log_w(TAG, "%s", s_status_note);
    }
}

void pm_app_wifi_cursor_move(int delta) {
    if (s_net_count == 0) return;
    s_cursor += delta;
    if (s_cursor < 0)              s_cursor = 0;
    if (s_cursor >= s_net_count)   s_cursor = s_net_count - 1;
}

// ─────────────────────────────────────────────
//  Auto-connect on boot — used by main.c
// ─────────────────────────────────────────────
void pm_wifi_autoconnect(void) {
    // Iterate keyring; first hit wins. Real implementation should
    // wait for scan results and pick the strongest known SSID, but
    // that's a follow-up.
    char ids[16 * 36];
    int n = pm_nosql_list("wifi_keyring", ids, 16, 36);
    if (n <= 0) {
        pm_log_i(TAG, "autoconnect: no saved networks");
        return;
    }
    char pass[64];
    const char* ssid = &ids[0];
    if (_keyring_lookup(ssid, pass, sizeof(pass))) {
        pm_log_i(TAG, "autoconnect: trying '%s'", ssid);
        pm_app_wifi_connect(ssid, pass);
    }
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    _refresh_status();
    _refresh_list();
    pm_log_d(TAG, "scanning=%d nets=%d connected=%d",
             s_scanning, s_net_count, s_connected);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static void _scan_cb(lv_event_t* e)       { (void)e; pm_app_wifi_action_scan(); }
static void _connect_cb(lv_event_t* e)    { (void)e; pm_app_wifi_connect_at_cursor(); }
static void _disconnect_cb(lv_event_t* e) { (void)e; pm_app_wifi_disconnect(); }

static void _list_row_cb(lv_event_t* e) {
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    s_cursor = (int)idx;
}

static void _refresh_list(void) {
    if (!s_list) return;
    lv_obj_clean(s_list);
    for (int i = 0; i < s_net_count; i++) {
        char row_text[80];
        snprintf(row_text, sizeof(row_text), "%-32s  %d dBm  ch%d  %s",
                  s_nets[i].ssid[0] ? s_nets[i].ssid : "(hidden)",
                  s_nets[i].rssi, s_nets[i].channel, s_nets[i].enc);
        lv_obj_t* btn = lv_list_add_button(s_list, LV_SYMBOL_WIFI, row_text);
        lv_obj_add_event_cb(btn, _list_row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

static void _refresh_status(void) {
    if (!s_status_lbl) return;
    char buf[96];
    if (s_status_note[0])
        snprintf(buf, sizeof(buf), "%s", s_status_note);
    else if (s_connected)
        snprintf(buf, sizeof(buf), "CONNECTED  %s @ %s", s_connected_ssid, s_connected_ip);
    else if (s_scanning)
        snprintf(buf, sizeof(buf), "SCANNING");
    else
        snprintf(buf, sizeof(buf), "DISCONNECTED  (%d networks seen)", s_net_count);
    lv_label_set_text(s_status_lbl, buf);
}

// duplicate-removed: static void _render(void) { _refresh_status(); _refresh_list(); }

static void _build_screen(void) {
    s_wifi_screen = pm_ui_screen();
    pm_ui_titlebar(s_wifi_screen, "WIFI", NULL, NULL);

    // Status bar + buttons
    lv_obj_t* status_card = pm_ui_card(s_wifi_screen);
    lv_obj_set_height(status_card, 80);
    s_status_lbl = lv_label_create(status_card);
    lv_label_set_text(s_status_lbl, "—");
    lv_obj_set_style_text_color(s_status_lbl, PM_C_FG, 0);

    lv_obj_t* btn_row = lv_obj_create(status_card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    pm_ui_button(btn_row, "Scan",       _scan_cb,       NULL);
    pm_ui_button(btn_row, "Connect",    _connect_cb,    NULL);
    pm_ui_button(btn_row, "Disconnect", _disconnect_cb, NULL);

    // Networks list
    s_list = pm_ui_list(s_wifi_screen);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_height(s_list, LV_PCT(100));
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_wifi_screen) lv_screen_load(s_wifi_screen);
    pm_log_i(TAG, "enter");
    _register_wifi_events();
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "wifi",
    .display_name = "WIFI",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_wifi(void) { return &_APP; }
