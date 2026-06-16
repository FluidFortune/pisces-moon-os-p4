// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_wardrive.c — SQLite session logger + viewer
//
//  Behaviors:
//    - Opens a per-boot DB on first event (lazy).
//    - Upsert pattern: SELECT id, hits → UPDATE/INSERT.
//      Faster than INSERT OR REPLACE for our column shape.
//    - Counters update HUD without redrawing.
//    - On exit, leaves DB on disk for next session resume
//      (host can use sqlite shell anytime).
//    - CSV export to /sd/exports/wardrive_<ts>.csv on demand.
// ============================================================

#include "pm_app_wardrive.h"
#include "pm_wardrive_schema.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pm_gps_state.h"
#include "pm_gps_uart.h"
#include "pm_board.h"
#include "pm_cardputer_i2c.h"
#include "pm_launcher.h"
#include "pm_peer.h"
#include "pm_ui.h"
#include "pm_sqlite.h"
#include "pm_tilemap.h"
#include "pm_gps_state.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_WARDRIVE";

#define WD_WIFI_SCAN_OWNER "wardrive"
#define SESSIONS_DIR "/sd/sessions"
#define EXPORTS_DIR  "/sd/exports"
#define WD_MAX_SCAN_RECORDS 64

#if PM_BOARD_LCD_H_RES <= 800
#define WD_LAYOUT_COMPACT 1
#define WD_MAX_UI_NETWORK_ROWS 7
#define WD_MAX_UI_LOG_ROWS 8
#define WD_TITLEBAR_H 38
#define WD_BACK_SIZE 30
#define WD_TITLE_PAD_H 8
#define WD_TITLE_GAP 6
#define WD_SESSION_W 132
#define WD_REC_DOT_SIZE 8
#define WD_STATS_H 68
#define WD_STAT_H 68
#define WD_LEFT_W 270
#define WD_RIGHT_W 280
#define WD_PANEL_HEADER_H 24
#define WD_ACTION_H 44
#define WD_ACTION_PAD_H 8
#define WD_ACTION_GAP 6
#define WD_ACTION_BTN_H 32
#define WD_ACTION_BTN_PAD_H 10
#define WD_ACTION_TEXT_SPACE 0
#define WD_ROW_TS_W 42
#define WD_ROW_BADGE_W 42
#define WD_FONT_STAT (&lv_font_montserrat_24)
#define WD_FONT_TEXT (&lv_font_montserrat_12)
#define WD_FONT_LABEL (&lv_font_montserrat_10)
#define WD_CENTER_TEXT "GPS / SIGNAL"
#else
#define WD_LAYOUT_COMPACT 0
#define WD_MAX_UI_NETWORK_ROWS 10
#define WD_MAX_UI_LOG_ROWS 12
#define WD_TITLEBAR_H 44
#define WD_BACK_SIZE 36
#define WD_TITLE_PAD_H 14
#define WD_TITLE_GAP 10
#define WD_SESSION_W 220
#define WD_REC_DOT_SIZE 10
#define WD_STATS_H 100
#define WD_STAT_H 100
#define WD_LEFT_W 280
#define WD_RIGHT_W 340
#define WD_PANEL_HEADER_H 28
#define WD_ACTION_H 52
#define WD_ACTION_PAD_H 16
#define WD_ACTION_GAP 12
#define WD_ACTION_BTN_H 36
#define WD_ACTION_BTN_PAD_H 20
#define WD_ACTION_TEXT_SPACE 2
#define WD_ROW_TS_W 56
#define WD_ROW_BADGE_W 48
#define WD_FONT_STAT (&lv_font_montserrat_28)
#define WD_FONT_TEXT (&lv_font_montserrat_14)
#define WD_FONT_LABEL (&lv_font_montserrat_14)
#define WD_CENTER_TEXT "GPS / SIGNAL VIEW"
#endif

#define WD_MAX_UI_LOGS_PER_SCAN 4
#define WD_MAX_UI_PENDING_LOGS (WD_MAX_UI_LOGS_PER_SCAN + 4)
#define WD_SCAN_RESTART_DELAY_MS 2500
#define WD_MAX_BLE_TRACKED 256

typedef struct {
    char ssid[33];
    char bssid[18];
    char enc[12];
    int rssi;
    int channel;
} wd_ui_network_t;

typedef struct {
    char timestamp[8];
    char type[8];
    char content[80];
    uint32_t color_hex;
} wd_ui_log_t;

static pm_db_t* s_db = NULL;
static char     s_session_path[80] = "";
static char     s_session_label[40] = "";
static bool     s_csv_fallback = true;   // CSV fast path by default
static bool     s_csv_live_paused = false;
static bool     s_csv_pause_logged = false;
static bool     s_csv_dir_checked = false;

// HUD counters
static int s_wifi_total = 0;
static int s_ble_total  = 0;
static int s_probe_total = 0;
static int s_pkt_total  = 0;
static char s_ble_seen_macs[WD_MAX_BLE_TRACKED][18];
static uint16_t s_ble_seen_count = 0;

static void _reset_ble_window(void) {
    s_ble_total = 0;
    s_ble_seen_count = 0;
    memset(s_ble_seen_macs, 0, sizeof(s_ble_seen_macs));
}

static bool _remember_ble_mac(const char* mac) {
    if (!mac || !mac[0]) return false;
    for (uint16_t i = 0; i < s_ble_seen_count; i++) {
        if (strncmp(s_ble_seen_macs[i], mac, sizeof(s_ble_seen_macs[i])) == 0) {
            return false;
        }
    }
    if (s_ble_seen_count >= WD_MAX_BLE_TRACKED) return false;
    snprintf(s_ble_seen_macs[s_ble_seen_count],
             sizeof(s_ble_seen_macs[s_ble_seen_count]), "%s", mac);
    s_ble_seen_count++;
    return true;
}

// LVGL handles
static lv_obj_t* s_net_list      = NULL;
static lv_obj_t* s_live_log      = NULL;
static lv_obj_t* s_center_canvas = NULL;
static lv_obj_t* s_chip_gps      = NULL;
static lv_obj_t* s_chip_db       = NULL;
static lv_obj_t* s_rec_dot       = NULL;
static lv_obj_t* s_btn_start     = NULL;
static lv_obj_t* s_btn_stop      = NULL;

// ── Map overlay (center panel) ──────────────────────────────
// The center panel hosts a slippy-map tile renderer with the
// GPS-status text floated on top. The map only shows imagery
// when a tile pack exists at /sd/tiles; otherwise it's a dark
// panel and the status text carries the information. We track
// the rover's path as a rolling polyline and drop a marker at
// the current fix.
static pm_tilemap_t* s_map         = NULL;   // NULL until built / after destroy
static lv_obj_t*     s_map_center  = NULL;   // the center container
static bool          s_follow_gps  = true;   // recenter on each new fix
static pm_tilemap_marker_t s_track[256];     // rolling path polyline
static int           s_track_n     = 0;
static pm_tilemap_marker_t s_pos_marker;     // single current-position dot
static double        s_last_track_lat = 0.0;
static double        s_last_track_lon = 0.0;
static bool          s_have_last_track = false;
// Default view: Oceanside, CA — sensible if a local tile pack
// is present before the first GPS fix arrives.
#define WD_MAP_DEFAULT_LAT   33.1959
#define WD_MAP_DEFAULT_LON  -117.3795
#define WD_MAP_DEFAULT_ZOOM  16
// Only re-record a track point / recenter when the fix has moved
// at least this far (~5 m), so a stationary rover doesn't thrash
// the renderer or flood the track buffer.
#define WD_MAP_MOVE_EPS_DEG  0.00005
static lv_obj_t* s_net_rows[WD_MAX_UI_NETWORK_ROWS];
static lv_obj_t* s_net_ssid_labels[WD_MAX_UI_NETWORK_ROWS];
static lv_obj_t* s_net_meta_labels[WD_MAX_UI_NETWORK_ROWS];
static lv_obj_t* s_log_rows[WD_MAX_UI_LOG_ROWS];
static lv_obj_t* s_log_ts_labels[WD_MAX_UI_LOG_ROWS];
static lv_obj_t* s_log_type_labels[WD_MAX_UI_LOG_ROWS];
static lv_obj_t* s_log_content_labels[WD_MAX_UI_LOG_ROWS];
static wd_ui_log_t s_log_display[WD_MAX_UI_LOG_ROWS];
static uint16_t s_log_display_count = 0;
static volatile bool s_running   = false;
static volatile bool s_foreground = false;
static TaskHandle_t s_scan_worker = NULL;
static SemaphoreHandle_t s_scan_ui_mutex = NULL;
static wd_ui_network_t s_ui_networks[WD_MAX_UI_NETWORK_ROWS];
static uint16_t s_ui_network_count = 0;
static wd_ui_log_t s_ui_logs[WD_MAX_UI_PENDING_LOGS];
static uint16_t s_ui_log_count = 0;
static bool s_ui_scan_dirty = false;
static volatile bool s_scan_visual_stop_pending = false;
static pm_peer_t* s_ble_peer = NULL;
static bool s_ble_peer_started = false;

void pm_app_wardrive_log(const char* timestamp, const char* type,
                         const char* content, lv_color_t color);
void pm_app_wardrive_add_network(const char* ssid, const char* bssid,
                                  int rssi, int channel, const char* enc);
static bool _ensure_scan_ui_mutex(void);
static void _queue_status_log(const char* timestamp, const char* type,
                              const char* content, uint32_t color_hex);

static void _ensure_session_label(void) {
    if (s_session_label[0]) return;
    uint32_t up = pm_uptime_seconds();
    snprintf(s_session_label, sizeof(s_session_label), "%010u", (unsigned)up);
}

// ─────────────────────────────────────────────
//  Lazy open
// ─────────────────────────────────────────────
static bool _ensure_db(void) {
    if (s_db) return true;
    _ensure_session_label();
    if (s_csv_fallback) return false;

    pm_file_mkdir(SESSIONS_DIR);
    pm_file_mkdir(EXPORTS_DIR);

    snprintf(s_session_path,  sizeof(s_session_path),
             "%s/session_%s.db", SESSIONS_DIR, s_session_label);

    s_db = pm_db_open(s_session_path);
    if (!s_db) {
        pm_log_w(TAG, "DB open failed — switching to CSV fallback");
        s_csv_fallback = true;
        return false;
    }
    if (!pm_db_apply_schema(s_db, PM_WARDRIVE_SCHEMA_SQL)) {
        pm_log_w(TAG, "schema apply failed: %s", pm_db_last_error(s_db));
        pm_db_close(s_db);
        s_db = NULL;
        s_csv_fallback = true;
        return false;
    }

    // metadata row — useful when host opens the DB.
    char buf[120];
    int n = snprintf(buf, sizeof(buf),
                      "INSERT OR REPLACE INTO metadata(key,value) "
                      "VALUES('session_label','%s');", s_session_label);
    (void)n;
    pm_db_exec(s_db, buf);
    pm_db_exec(s_db, "INSERT OR REPLACE INTO metadata(key,value) "
                      "VALUES('schema_version','1');");
    return true;
}

void pm_app_wardrive_use_csv_fallback(bool on) {
    s_csv_fallback = on;
    if (on && s_db) { pm_db_close(s_db); s_db = NULL; }
    pm_log_i(TAG, "fallback=%d", (int)on);
}

// ─────────────────────────────────────────────
//  Insert helpers
// ─────────────────────────────────────────────
static void _gps_now(double* lat, double* lng) {
    pm_gps_t g; pm_gps_state_get(&g);
    *lat = g.valid ? g.lat : 0.0;
    *lng = g.valid ? g.lng : 0.0;
}

static bool _csv_fallback_append_wifi(const char* bssid, const char* ssid,
                                       const char* enc, int channel, int rssi,
                                       double lat, double lng) {
    if (s_csv_live_paused) return false;

    _ensure_session_label();
    if (!s_csv_dir_checked) {
        pm_file_mkdir(EXPORTS_DIR);
        s_csv_dir_checked = true;
    }
    char path[80];
    snprintf(path, sizeof(path), "%s/wardrive_%s.csv",
             EXPORTS_DIR, s_session_label[0] ? s_session_label : "fallback");
    char line[256];
    int n = snprintf(line, sizeof(line),
                      "%s,%s,%s,%u,%d,%d,%.6f,%.6f,0,0,WIFI\n",
                      bssid, ssid ? ssid : "",
                      enc   ? enc  : "",
                      (unsigned)pm_uptime_seconds(),
                      channel, rssi, lat, lng);
    bool ok = false;
    PM_SPI_TAKE("wd_csv_wifi") {
        pm_file_t* f = pm_file_open(path, PM_FILE_APPEND | PM_FILE_CREATE);
        if (f) {
            ok = (pm_file_write(f, line, n) == (size_t)n);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    if (!ok) {
        s_csv_live_paused = true;
        pm_log_w(TAG, "CSV live logging paused after SD write/open failure");
    }
    return ok;
}

void pm_app_wardrive_on_wifi(const char* bssid, const char* ssid,
                              int rssi, int channel, const char* enc) {
    if (!s_running) return;
    if (!bssid) return;
    s_wifi_total++;
    double lat, lng; _gps_now(&lat, &lng);
    uint32_t now = pm_millis();

    if (!_ensure_db()) {
        (void)_csv_fallback_append_wifi(bssid, ssid, enc, channel, rssi, lat, lng);
        return;
    }

    // Upsert
    pm_stmt_t* sel = pm_db_prepare(s_db,
        "SELECT id, hits FROM wifi_seen WHERE bssid = ?;");
    if (!sel) return;
    pm_stmt_bind_text(sel, 1, bssid);
    if (pm_stmt_step(sel)) {
        int id   = pm_stmt_col_int(sel, 0);
        int hits = pm_stmt_col_int(sel, 1);
        pm_stmt_finalize(sel);

        pm_stmt_t* up = pm_db_prepare(s_db,
            "UPDATE wifi_seen SET rssi=?, last_ms=?, hits=? WHERE id=?;");
        if (up) {
            pm_stmt_bind_int(up, 1, rssi);
            pm_stmt_bind_int64(up, 2, (int64_t)now);
            pm_stmt_bind_int(up, 3, hits + 1);
            pm_stmt_bind_int(up, 4, id);
            pm_stmt_step(up);
            pm_stmt_finalize(up);
        }
    } else {
        pm_stmt_finalize(sel);
        pm_stmt_t* ins = pm_db_prepare(s_db,
            "INSERT INTO wifi_seen(bssid,ssid,rssi,channel,enc,lat,lng,first_ms,last_ms,hits) "
            "VALUES(?,?,?,?,?,?,?,?,?,1);");
        if (ins) {
            pm_stmt_bind_text(ins, 1, bssid);
            pm_stmt_bind_text(ins, 2, ssid ? ssid : "");
            pm_stmt_bind_int (ins, 3, rssi);
            pm_stmt_bind_int (ins, 4, channel);
            pm_stmt_bind_text(ins, 5, enc ? enc : "");
            pm_stmt_bind_double(ins, 6, lat);
            pm_stmt_bind_double(ins, 7, lng);
            pm_stmt_bind_int64 (ins, 8, (int64_t)now);
            pm_stmt_bind_int64 (ins, 9, (int64_t)now);
            pm_stmt_step(ins);
            pm_stmt_finalize(ins);
        }
    }
}

void pm_app_wardrive_on_ble(const char* mac, const char* name,
                              int rssi, const char* addr_type, const char* mfg) {
    if (!s_running) return;
    if (!mac) return;
    if (_remember_ble_mac(mac)) s_ble_total++;
    double lat, lng; _gps_now(&lat, &lng);
    uint32_t now = pm_millis();

    if (!_ensure_db()) return;

    pm_stmt_t* sel = pm_db_prepare(s_db,
        "SELECT id, hits FROM ble_seen WHERE mac = ?;");
    if (!sel) return;
    pm_stmt_bind_text(sel, 1, mac);
    if (pm_stmt_step(sel)) {
        int id   = pm_stmt_col_int(sel, 0);
        int hits = pm_stmt_col_int(sel, 1);
        pm_stmt_finalize(sel);

        pm_stmt_t* up = pm_db_prepare(s_db,
            "UPDATE ble_seen SET rssi=?, last_ms=?, hits=? WHERE id=?;");
        if (up) {
            pm_stmt_bind_int(up, 1, rssi);
            pm_stmt_bind_int64(up, 2, (int64_t)now);
            pm_stmt_bind_int(up, 3, hits + 1);
            pm_stmt_bind_int(up, 4, id);
            pm_stmt_step(up);
            pm_stmt_finalize(up);
        }
    } else {
        pm_stmt_finalize(sel);
        pm_stmt_t* ins = pm_db_prepare(s_db,
            "INSERT INTO ble_seen(mac,name,rssi,addr_type,mfg,lat,lng,first_ms,last_ms,hits) "
            "VALUES(?,?,?,?,?,?,?,?,?,1);");
        if (ins) {
            pm_stmt_bind_text(ins, 1, mac);
            pm_stmt_bind_text(ins, 2, name ? name : "");
            pm_stmt_bind_int (ins, 3, rssi);
            pm_stmt_bind_text(ins, 4, addr_type ? addr_type : "");
            pm_stmt_bind_text(ins, 5, mfg ? mfg : "");
            pm_stmt_bind_double(ins, 6, lat);
            pm_stmt_bind_double(ins, 7, lng);
            pm_stmt_bind_int64 (ins, 8, (int64_t)now);
            pm_stmt_bind_int64 (ins, 9, (int64_t)now);
            pm_stmt_step(ins);
            pm_stmt_finalize(ins);
        }
    }
}

void pm_app_wardrive_on_probe(const char* mac, const char* ssid,
                                int rssi, int count) {
    if (!s_running) return;
    if (!mac) return;
    s_probe_total++;
    (void)count;
    double lat, lng; _gps_now(&lat, &lng);
    if (!_ensure_db()) return;
    pm_stmt_t* ins = pm_db_prepare(s_db,
        "INSERT INTO probes(mac,ssid,rssi,lat,lng,first_ms,last_ms,hits) "
        "VALUES(?,?,?,?,?,?,?,1);");
    if (!ins) return;
    uint32_t now = pm_millis();
    pm_stmt_bind_text(ins, 1, mac);
    pm_stmt_bind_text(ins, 2, ssid ? ssid : "");
    pm_stmt_bind_int (ins, 3, rssi);
    pm_stmt_bind_double(ins, 4, lat);
    pm_stmt_bind_double(ins, 5, lng);
    pm_stmt_bind_int64 (ins, 6, (int64_t)now);
    pm_stmt_bind_int64 (ins, 7, (int64_t)now);
    pm_stmt_step(ins);
    pm_stmt_finalize(ins);
}

void pm_app_wardrive_on_pkt(const char* frame_type, const char* src, int rssi) {
    if (!s_running) return;
    s_pkt_total++;
    if (!frame_type) return;
    double lat, lng; _gps_now(&lat, &lng);
    if (!_ensure_db()) return;
    pm_stmt_t* ins = pm_db_prepare(s_db,
        "INSERT INTO packets(frame_type,src,dst,rssi,lat,lng,ts_ms) "
        "VALUES(?,?,?,?,?,?,?);");
    if (!ins) return;
    pm_stmt_bind_text(ins, 1, frame_type);
    pm_stmt_bind_text(ins, 2, src ? src : "");
    pm_stmt_bind_text(ins, 3, "");
    pm_stmt_bind_int (ins, 4, rssi);
    pm_stmt_bind_double(ins, 5, lat);
    pm_stmt_bind_double(ins, 6, lng);
    pm_stmt_bind_int64 (ins, 7, (int64_t)pm_millis());
    pm_stmt_step(ins);
    pm_stmt_finalize(ins);
}

// ─────────────────────────────────────────────
//  CSV export — Jennifer-compatible column order
// ─────────────────────────────────────────────
bool pm_app_wardrive_export_csv(void) {
    if (s_csv_fallback) return true;     // already CSV
    if (!s_db) return false;
    pm_file_mkdir(EXPORTS_DIR);
    char path[80];
    snprintf(path, sizeof(path), "%s/wardrive_%s.csv", EXPORTS_DIR, s_session_label);

    int rows = pm_db_export_csv(s_db,
        "SELECT bssid AS MAC, ssid AS SSID, enc AS AuthMode, "
        "first_ms AS FirstSeen, channel AS Channel, rssi AS RSSI, "
        "lat AS CurrentLatitude, lng AS CurrentLongitude, "
        "0 AS AltitudeMeters, 0 AS AccuracyMeters, 'WIFI' AS Type "
        "FROM wifi_seen ORDER BY last_ms;",
        path);
    pm_log_i(TAG, "export wrote %d rows to %s", rows, path);
    return rows >= 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static lv_obj_t* s_lbl_session  = NULL;
static lv_obj_t* s_lbl_wifi  = NULL;
static lv_obj_t* s_lbl_ble   = NULL;
static lv_obj_t* s_lbl_probe = NULL;
static lv_obj_t* s_lbl_pkt   = NULL;
static int s_last_wifi_total = -1;
static int s_last_ble_total = -1;
static int s_last_probe_total = -1;
static int s_last_pkt_total = -1;
static char s_last_session_text[72] = "";
static char s_last_gps_chip_text[16] = "";
static char s_last_center_text[128] = "";

static void _reset_capture_counters(void) {
    s_wifi_total = 0;
    s_probe_total = 0;
    s_pkt_total = 0;
    _reset_ble_window();
    s_last_wifi_total = -1;
    s_last_ble_total = -1;
    s_last_probe_total = -1;
    s_last_pkt_total = -1;
}

static void _label_set_text_changed(lv_obj_t* label, char* cache,
                                    size_t cache_len, const char* text) {
    if (!label || !cache || cache_len == 0 || !text) return;
    if (strncmp(cache, text, cache_len) == 0) return;
    snprintf(cache, cache_len, "%s", text);
    lv_label_set_text(label, cache);
}

static void __attribute__((unused)) _trim_children(lv_obj_t* parent, uint32_t max_children) {
    if (!parent) return;
    while (lv_obj_get_child_count(parent) > max_children) {
        lv_obj_t* oldest = lv_obj_get_child(parent, 0);
        if (!oldest) break;
        lv_obj_delete(oldest);
    }
}

static void _set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void _ensure_network_row_widgets(void) {
    if (!s_net_list) return;
    for (uint16_t i = 0; i < WD_MAX_UI_NETWORK_ROWS; i++) {
        if (s_net_rows[i]) continue;

        lv_obj_t* item = lv_obj_create(s_net_list);
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_pad_left(item, 12, 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0x1a3a50), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);

        lv_obj_t* ssid_lbl = lv_label_create(item);
        lv_label_set_text(ssid_lbl, "");
        lv_obj_set_style_text_font(ssid_lbl, WD_FONT_TEXT, 0);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xc8e8f5), 0);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(ssid_lbl, LV_PCT(100));

        lv_obj_t* meta_lbl = lv_label_create(item);
        lv_label_set_text(meta_lbl, "");
        lv_obj_set_style_text_font(meta_lbl, WD_FONT_LABEL, 0);
        lv_obj_set_style_text_color(meta_lbl, lv_color_hex(0x2a5870), 0);
        lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(meta_lbl, LV_PCT(100));

        s_net_rows[i] = item;
        s_net_ssid_labels[i] = ssid_lbl;
        s_net_meta_labels[i] = meta_lbl;
        _set_hidden(item, true);
    }
}

static void _ensure_log_row_widgets(void) {
    if (!s_live_log) return;
    for (uint16_t i = 0; i < WD_MAX_UI_LOG_ROWS; i++) {
        if (s_log_rows[i]) continue;

        lv_obj_t* row = lv_obj_create(s_live_log);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_ver(row, WD_LAYOUT_COMPACT ? 2 : 3, 0);
        lv_obj_set_style_pad_hor(row, WD_LAYOUT_COMPACT ? 6 : 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x1a3a50), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);

        lv_obj_t* ts = lv_label_create(row);
        lv_label_set_text(ts, "");
        lv_obj_set_style_text_font(ts, WD_FONT_LABEL, 0);
        lv_obj_set_style_text_color(ts, lv_color_hex(0x2a5870), 0);
        lv_obj_set_width(ts, WD_ROW_TS_W);

        lv_obj_t* badge = lv_label_create(row);
        lv_label_set_text(badge, "");
        lv_obj_set_style_text_font(badge, WD_FONT_LABEL, 0);
        lv_obj_set_width(badge, WD_ROW_BADGE_W);

        lv_obj_t* cnt = lv_label_create(row);
        lv_label_set_text(cnt, "");
        lv_obj_set_style_text_font(cnt, WD_FONT_LABEL, 0);
        lv_obj_set_style_text_color(cnt, lv_color_hex(0xc8e8f5), 0);
        lv_obj_set_flex_grow(cnt, 1);
        lv_label_set_long_mode(cnt, LV_LABEL_LONG_CLIP);

        s_log_rows[i] = row;
        s_log_ts_labels[i] = ts;
        s_log_type_labels[i] = badge;
        s_log_content_labels[i] = cnt;
        _set_hidden(row, true);
    }
}

static void _update_network_rows(const wd_ui_network_t* networks,
                                 uint16_t network_count) {
    _ensure_network_row_widgets();
    for (uint16_t i = 0; i < WD_MAX_UI_NETWORK_ROWS; i++) {
        if (i >= network_count) {
            _set_hidden(s_net_rows[i], true);
            continue;
        }

        char meta[80];
        snprintf(meta, sizeof(meta), "%s  CH%d  %ddBm  %s",
                 networks[i].bssid[0] ? networks[i].bssid : "--:--:--:--:--:--",
                 networks[i].channel,
                 networks[i].rssi,
                 networks[i].enc[0] ? networks[i].enc : "OPEN");

        if (s_net_ssid_labels[i]) {
            lv_label_set_text(s_net_ssid_labels[i],
                              networks[i].ssid[0] ? networks[i].ssid : "(hidden)");
        }
        if (s_net_meta_labels[i]) lv_label_set_text(s_net_meta_labels[i], meta);
        _set_hidden(s_net_rows[i], false);
    }
}

static void _update_log_rows(void) {
    _ensure_log_row_widgets();
    for (uint16_t i = 0; i < WD_MAX_UI_LOG_ROWS; i++) {
        if (i >= s_log_display_count) {
            _set_hidden(s_log_rows[i], true);
            continue;
        }
        const wd_ui_log_t* log = &s_log_display[i];
        if (s_log_ts_labels[i]) lv_label_set_text(s_log_ts_labels[i], log->timestamp);
        if (s_log_type_labels[i]) {
            lv_label_set_text(s_log_type_labels[i], log->type);
            lv_obj_set_style_text_color(s_log_type_labels[i],
                                        lv_color_hex(log->color_hex), 0);
        }
        if (s_log_content_labels[i]) {
            lv_label_set_text(s_log_content_labels[i], log->content);
        }
        _set_hidden(s_log_rows[i], false);
    }
}

static void _append_display_log(const wd_ui_log_t* log) {
    if (!log) return;
    if (s_log_display_count >= WD_MAX_UI_LOG_ROWS) {
        memmove(&s_log_display[0], &s_log_display[1],
                (WD_MAX_UI_LOG_ROWS - 1) * sizeof(s_log_display[0]));
        s_log_display_count = WD_MAX_UI_LOG_ROWS - 1;
    }
    s_log_display[s_log_display_count++] = *log;
}

static void _flush_scan_ui(void) {
    wd_ui_network_t networks[WD_MAX_UI_NETWORK_ROWS];
    wd_ui_log_t logs[WD_MAX_UI_PENDING_LOGS];
    uint16_t network_count = 0;
    uint16_t log_count = 0;
    bool dirty = false;

    if (_ensure_scan_ui_mutex() &&
        xSemaphoreTake(s_scan_ui_mutex, 0) == pdTRUE) {
        dirty = s_ui_scan_dirty;
        network_count = s_ui_network_count;
        if (network_count > WD_MAX_UI_NETWORK_ROWS) {
            network_count = WD_MAX_UI_NETWORK_ROWS;
        }
        log_count = s_ui_log_count;
        if (log_count > WD_MAX_UI_PENDING_LOGS) {
            log_count = WD_MAX_UI_PENDING_LOGS;
        }
        memcpy(networks, s_ui_networks, network_count * sizeof(networks[0]));
        memcpy(logs, s_ui_logs, log_count * sizeof(logs[0]));
        s_ui_log_count = 0;
        s_ui_scan_dirty = false;
        xSemaphoreGive(s_scan_ui_mutex);
    }

    if (dirty) {
        _update_network_rows(networks, network_count);
        for (uint16_t i = 0; i < log_count; i++) {
            _append_display_log(&logs[i]);
        }
        _update_log_rows();
    }

    if (s_scan_visual_stop_pending) {
        s_scan_visual_stop_pending = false;
        if (s_rec_dot) {
            lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(0x2a5870), 0);
        }
        if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, 25, 0);
    }
}

// ── Map: fold a new GPS fix into the center-panel renderer ──
// Called from _render() under the LVGL lock. Cheap when the
// rover is stationary: we only touch the tilemap when the fix
// has actually moved (WD_MAP_MOVE_EPS_DEG), so a parked device
// doesn't thrash the tile layout or flood the track buffer.
static void _update_map_from_gps(const pm_gps_t* g) {
    if (!s_map || !g || !g->valid) return;

    bool moved = true;
    if (s_have_last_track) {
        double dlat = g->lat - s_last_track_lat;
        double dlon = g->lng - s_last_track_lon;
        if (dlat < 0) dlat = -dlat;
        if (dlon < 0) dlon = -dlon;
        moved = (dlat >= WD_MAP_MOVE_EPS_DEG || dlon >= WD_MAP_MOVE_EPS_DEG);
    }
    if (!moved) return;

    s_last_track_lat  = g->lat;
    s_last_track_lon  = g->lng;
    s_have_last_track = true;

    // Append to the rolling track polyline.
    if (s_track_n < (int)(sizeof(s_track) / sizeof(s_track[0]))) {
        s_track[s_track_n].lat       = g->lat;
        s_track[s_track_n].lon       = g->lng;
        s_track[s_track_n].color     = lv_color_hex(0x4dd9ff);
        s_track[s_track_n].radius_px = 0;
        s_track[s_track_n].label     = NULL;
        s_track_n++;
    } else {
        memmove(&s_track[0], &s_track[1],
                sizeof(s_track[0]) * (s_track_n - 1));
        s_track[s_track_n - 1].lat = g->lat;
        s_track[s_track_n - 1].lon = g->lng;
    }

    // Current-position marker (cyan dot).
    s_pos_marker.lat       = g->lat;
    s_pos_marker.lon       = g->lng;
    s_pos_marker.color     = lv_color_hex(0x4dd9ff);
    s_pos_marker.radius_px = 7;
    s_pos_marker.label     = NULL;

    // Recenter first (if following), then push overlays. Each of
    // these relayouts; on a stationary rover none of this runs.
    if (s_follow_gps) {
        pm_tilemap_set_center(s_map, g->lat, g->lng);
    }
    pm_tilemap_set_markers(s_map, &s_pos_marker, 1);
    pm_tilemap_set_track(s_map, s_track, s_track_n);
}

static void _render(void) {
    _ensure_session_label();
    if (s_lbl_session) {
        char buf[72];
        snprintf(buf, sizeof(buf), "session_%s%s",
                  s_session_label,
                  s_csv_fallback
                    ? (s_csv_live_paused ? " (CSV paused)" : " (CSV fallback)")
                    : "");
        _label_set_text_changed(s_lbl_session, s_last_session_text,
                                sizeof(s_last_session_text), buf);
    }
    char nbuf[16];
    if (s_lbl_wifi && s_last_wifi_total != s_wifi_total) {
        snprintf(nbuf, sizeof(nbuf), "%d", s_wifi_total);
        lv_label_set_text(s_lbl_wifi, nbuf);
        s_last_wifi_total = s_wifi_total;
    }
    if (s_lbl_ble && s_last_ble_total != s_ble_total) {
        snprintf(nbuf, sizeof(nbuf), "%d", s_ble_total);
        lv_label_set_text(s_lbl_ble, nbuf);
        s_last_ble_total = s_ble_total;
    }
    if (s_lbl_probe && s_last_probe_total != s_probe_total) {
        snprintf(nbuf, sizeof(nbuf), "%d", s_probe_total);
        lv_label_set_text(s_lbl_probe, nbuf);
        s_last_probe_total = s_probe_total;
    }
    if (s_lbl_pkt && s_last_pkt_total != s_pkt_total) {
        snprintf(nbuf, sizeof(nbuf), "%d", s_pkt_total);
        lv_label_set_text(s_lbl_pkt, nbuf);
        s_last_pkt_total = s_pkt_total;
    }
    // GPS chip live update
    if (s_chip_gps) {
        pm_gps_t g; pm_gps_state_get(&g);
#if PM_BOARD_LOCAL_GPS_UART
        pm_gps_uart_stats_t st; pm_gps_uart_stats(&st);
#endif
        lv_obj_t* lbl = lv_obj_get_child(s_chip_gps, 0);
        char buf[16];
        lv_color_t c;
        if (g.valid) {
            snprintf(buf, sizeof(buf), "GPS %d", (int)g.sats);
            c = lv_color_hex(0x00ff88);
        } else if (g.sats > 0) {
            snprintf(buf, sizeof(buf), "GPS %d?", (int)g.sats);
            c = lv_color_hex(0xffcc00);
        } else {
            snprintf(buf, sizeof(buf), "GPS --");
            c = lv_color_hex(0x2a5870);
        }
        if (lbl) {
            _label_set_text_changed(lbl, s_last_gps_chip_text,
                                    sizeof(s_last_gps_chip_text), buf);
            lv_obj_set_style_text_color(lbl, c, 0);
        }
        lv_obj_set_style_border_color(s_chip_gps, c, 0);

        if (s_center_canvas) {
            char gps_buf[128];
            if (g.valid) {
                snprintf(gps_buf, sizeof(gps_buf),
                         "GPS FIX\n%+.6f, %+.6f\nsats %d  age %ums",
                         g.lat, g.lng, g.sats,
                         (unsigned)(pm_millis() - g.last_update_ms));
            } else if (g.last_update_ms != 0) {
#if PM_BOARD_LOCAL_GPS_UART
                snprintf(gps_buf, sizeof(gps_buf),
                         "GPS NO FIX\nsats %d\nNMEA %u good / %u bad",
                         g.sats,
                         (unsigned)st.sentences_seen,
                         (unsigned)st.sentences_bad);
#else
                snprintf(gps_buf, sizeof(gps_buf),
                         "GPS NO FIX\nCardputer header\nsats %d",
                         g.sats);
#endif
            } else {
#if PM_BOARD_LOCAL_GPS_UART
                if (st.bytes_rx == 0) {
                    snprintf(gps_buf, sizeof(gps_buf),
                             "GPS WAITING\nIO%d @ %u baud\nno UART bytes yet%s",
                             st.active_rx_pin ? st.active_rx_pin : PM_GPS_PIN_RX,
                             (unsigned)st.active_baud,
                             st.using_swapped_pins ? "\nprobing swapped pins" : "");
                } else {
                    snprintf(gps_buf, sizeof(gps_buf),
                             "GPS UART ACTIVE\nIO%d @ %u baud  %u bytes\n%u good / %u bad NMEA",
                             st.active_rx_pin ? st.active_rx_pin : PM_GPS_PIN_RX,
                             (unsigned)st.active_baud,
                             (unsigned)st.bytes_rx,
                             (unsigned)st.sentences_seen,
                             (unsigned)st.sentences_bad);
                }
#else
                snprintf(gps_buf, sizeof(gps_buf),
                         "GPS WAITING\nCardputer ADV UART1\nno GPS updates yet");
#endif
            }
            _label_set_text_changed(s_center_canvas, s_last_center_text,
                                    sizeof(s_last_center_text), gps_buf);
        }

        // Fold the same fix into the map renderer (no-op when the
        // rover hasn't moved, or when tiles/map aren't present).
        _update_map_from_gps(&g);
    }

    _flush_scan_ui();
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_wd_screen = NULL;

static void __attribute__((unused)) _export_cb(lv_event_t* e) {
    (void)e;
    pm_app_wardrive_export_csv();
}

static void _map_cb(lv_event_t* e) {
    (void)e;
    if (!s_map) return;
    // Toggle follow mode; when (re)enabling, snap to the current
    // fix immediately so the user sees the jump.
    s_follow_gps = !s_follow_gps;
    pm_gps_t g; pm_gps_state_get(&g);
    if (g.valid) {
        if (s_follow_gps) pm_tilemap_set_center(s_map, g.lat, g.lng);
        _queue_status_log("MAP", "GPS",
            s_follow_gps ? "follow on — centered on fix" : "follow off",
            0xf4a820);
    } else {
        _queue_status_log("MAP", "GPS",
            s_follow_gps ? "follow on (waiting for fix)" : "follow off",
            0xffcc00);
    }
}
static void _settings_cb(lv_event_t* e) { (void)e; pm_log_i("WD", "settings todo"); }
static void _back_cb(lv_event_t* e)     { (void)e; pm_launcher_back_from_app(); }

static const char* _auth_to_str(wifi_auth_mode_t auth) {
    switch (auth) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "WPA";
    }
}

static void _copy_text(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static void _format_bssid(const uint8_t bssid[6], char* out, size_t out_len) {
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static bool _ensure_scan_ui_mutex(void) {
    if (s_scan_ui_mutex) return true;
    s_scan_ui_mutex = xSemaphoreCreateMutex();
    if (!s_scan_ui_mutex) {
        pm_log_w("WARDRIVE", "scan UI mutex create failed");
        return false;
    }
    return true;
}

static void _append_ui_log_locked(const char* timestamp, const char* type,
                                  const char* content, uint32_t color_hex) {
    if (s_ui_log_count >= WD_MAX_UI_PENDING_LOGS) {
        memmove(&s_ui_logs[0], &s_ui_logs[1],
                (WD_MAX_UI_PENDING_LOGS - 1) * sizeof(s_ui_logs[0]));
        s_ui_log_count = WD_MAX_UI_PENDING_LOGS - 1;
    }
    wd_ui_log_t* log = &s_ui_logs[s_ui_log_count++];
    _copy_text(log->timestamp, sizeof(log->timestamp), timestamp);
    _copy_text(log->type, sizeof(log->type), type);
    _copy_text(log->content, sizeof(log->content), content);
    log->color_hex = color_hex;
}

static void _queue_status_log(const char* timestamp, const char* type,
                              const char* content, uint32_t color_hex) {
    if (!_ensure_scan_ui_mutex()) return;
    if (xSemaphoreTake(s_scan_ui_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    _append_ui_log_locked(timestamp, type, content, color_hex);
    s_ui_scan_dirty = true;
    xSemaphoreGive(s_scan_ui_mutex);
}

static void _queue_csv_pause_log(void) {
    if (s_csv_pause_logged) return;
    s_csv_pause_logged = true;
    _queue_status_log("SD", "CSV", "live logging paused", 0xffcc00);
}

static void _queue_scan_results(const wifi_ap_record_t* records,
                                uint16_t got, uint16_t found) {
    if (!_ensure_scan_ui_mutex()) return;
    if (xSemaphoreTake(s_scan_ui_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    uint16_t ui_nets = got;
    if (ui_nets > WD_MAX_UI_NETWORK_ROWS) ui_nets = WD_MAX_UI_NETWORK_ROWS;
    s_ui_network_count = ui_nets;
    for (uint16_t i = 0; i < ui_nets; i++) {
        const wifi_ap_record_t* ap = &records[i];
        wd_ui_network_t* row = &s_ui_networks[i];
        _copy_text(row->ssid, sizeof(row->ssid), (const char*)ap->ssid);
        _format_bssid(ap->bssid, row->bssid, sizeof(row->bssid));
        _copy_text(row->enc, sizeof(row->enc), _auth_to_str(ap->authmode));
        row->rssi = ap->rssi;
        row->channel = ap->primary;
    }

    uint16_t ui_logs = got;
    if (ui_logs > WD_MAX_UI_LOGS_PER_SCAN) ui_logs = WD_MAX_UI_LOGS_PER_SCAN;
    for (uint16_t i = 0; i < ui_logs; i++) {
        const wifi_ap_record_t* ap = &records[i];
        char ts[8];
        uint32_t up_sec = pm_uptime_seconds();
        snprintf(ts, sizeof(ts), "%02u:%02u",
                 (unsigned)((up_sec / 60) % 60),
                 (unsigned)(up_sec % 60));
        char content[80];
        snprintf(content, sizeof(content), "%-20.20s %ddBm CH%d",
                 (const char*)ap->ssid, ap->rssi, ap->primary);
        _append_ui_log_locked(ts, "WIFI", content, 0x00d4ff);
    }

    if (found == 0) {
        _append_ui_log_locked("SCAN", "WIFI", "0 APs returned", 0x4a7a92);
    } else if (found > ui_nets) {
        char msg[80];
        snprintf(msg, sizeof(msg), "%u APs shown, %u found",
                 (unsigned)ui_nets, (unsigned)found);
        _append_ui_log_locked("SCAN", "WIFI", msg, 0x4a7a92);
    }

    s_ui_scan_dirty = true;
    xSemaphoreGive(s_scan_ui_mutex);
}

static esp_err_t _start_wifi_scan(void) {
    wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 250 } }
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        pm_log_w("WARDRIVE", "scan_start failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void _stop_ble_source(void) {
    if (!s_ble_peer) return;
    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (s_ble_peer_started &&
        (kind == PM_PEER_KIND_CARDPUTER_I2C || kind == PM_PEER_KIND_TBEAM_S3)) {
        pm_peer_call(s_ble_peer, "ble_scan_stop", NULL);
    }
    pm_log_i(TAG, "BLE source stopped on %s", pm_peer_name(s_ble_peer));
    pm_peer_release_cap(s_ble_peer, "ble_scan");
    s_ble_peer = NULL;
    s_ble_peer_started = false;
}

static void _start_ble_source(void) {
    if (s_ble_peer) return;

    s_ble_peer = pm_peer_find("ble_scan", PM_PEER_ROLE_EXCLUSIVE);
    if (!s_ble_peer) {
        _queue_status_log("BLE", "WARN", "no BLE scan peer", 0xffcc00);
        return;
    }

    pm_peer_kind_t kind = pm_peer_kind(s_ble_peer);
    if (kind == PM_PEER_KIND_CARDPUTER_I2C) {
        int rc = pm_peer_call(s_ble_peer, "ble_scan_start", "\"active\":0");
        if (rc != 0) {
            pm_log_w(TAG, "Cardputer BLE start failed rc=%d", rc);
            _queue_status_log("BLE", "ERR", "Cardputer BLE start failed", 0xff3366);
            pm_peer_release_cap(s_ble_peer, "ble_scan");
            s_ble_peer = NULL;
            return;
        }
        s_ble_peer_started = true;
        _queue_status_log("BLE", "CARD", "Cardputer BLE scan active", 0x00ff88);
    } else if (kind == PM_PEER_KIND_TBEAM_S3) {
        int rc = pm_peer_call(s_ble_peer, "ble_scan_start", NULL);
        if (rc != 0) {
            pm_log_w(TAG, "T-Beam BLE start failed rc=%d", rc);
            _queue_status_log("BLE", "ERR", "T-Beam BLE start failed", 0xff3366);
            pm_peer_release_cap(s_ble_peer, "ble_scan");
            s_ble_peer = NULL;
            return;
        }
        s_ble_peer_started = true;
        _queue_status_log("BLE", "TBEAM", "T-Beam BLE scan active", 0x00ff88);
    } else {
        _queue_status_log("BLE", "C6", "local BLE fallback", 0xffcc00);
    }

    pm_log_i(TAG, "BLE source selected: %s", pm_peer_name(s_ble_peer));
}

static void _poll_external_ble(void) {
    if (!s_running || !s_ble_peer) return;
    if (pm_peer_kind(s_ble_peer) != PM_PEER_KIND_CARDPUTER_I2C) return;

    for (int i = 0; i < 8; i++) {
        pm_cardputer_i2c_ble_seen_t b = {0};
        esp_err_t err = pm_cardputer_i2c_ble_seen_pop(&b);
        if (err != ESP_OK || !b.available) break;

        pm_app_wardrive_on_ble(b.mac, b.name, b.rssi, b.addr_type, b.mfg);
        if (i < 3) {
            char ts[8];
            uint32_t up_sec = pm_uptime_seconds();
            snprintf(ts, sizeof(ts), "%02u:%02u",
                     (unsigned)((up_sec / 60) % 60),
                     (unsigned)(up_sec % 60));
            char content[80];
            snprintf(content, sizeof(content), "%-18.18s %ddBm %s",
                     b.mac, b.rssi, b.name[0] ? b.name : "BLE");
            _queue_status_log(ts, "BLE", content, 0x00ff88);
        }
    }
}

static void _process_scan_done(void) {
    if (!pm_wifi_scan_is_owner(WD_WIFI_SCAN_OWNER)) return;

    uint16_t found = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) {
        pm_log_w("WARDRIVE", "scan_get_ap_num failed: %s", esp_err_to_name(err));
        _queue_status_log("SCAN", "ERR", esp_err_to_name(err), 0xff3366);
        return;
    }

    uint16_t wanted = found;
    if (wanted > WD_MAX_SCAN_RECORDS) wanted = WD_MAX_SCAN_RECORDS;
    if (wanted == 0) {
        s_wifi_total = 0;
        _queue_scan_results(NULL, 0, 0);
        esp_wifi_clear_ap_list();
        _reset_ble_window();
        return;
    }

    wifi_ap_record_t* records =
        (wifi_ap_record_t*)pm_psram_calloc(wanted, sizeof(wifi_ap_record_t));
    if (!records) {
        records = (wifi_ap_record_t*)calloc(wanted, sizeof(wifi_ap_record_t));
    }
    if (!records) {
        _queue_status_log("SCAN", "ERR", "scan result allocation failed",
                          0xff3366);
        esp_wifi_clear_ap_list();
        return;
    }

    uint16_t got = wanted;
    err = esp_wifi_scan_get_ap_records(&got, records);
    if (err != ESP_OK) {
        pm_psram_free(records);
        pm_log_w("WARDRIVE", "scan_get_ap_records failed: %s", esp_err_to_name(err));
        _queue_status_log("SCAN", "ERR", esp_err_to_name(err), 0xff3366);
        esp_wifi_clear_ap_list();
        return;
    }
    esp_wifi_clear_ap_list();

    s_wifi_total = found;
    _queue_scan_results(records, got, found);
    if (s_csv_live_paused) _queue_csv_pause_log();

    for (uint16_t i = 0; i < got && s_csv_fallback && !s_csv_live_paused; i++) {
        const wifi_ap_record_t* ap = &records[i];
        char bssid_str[18];
        _format_bssid(ap->bssid, bssid_str, sizeof(bssid_str));
        const char* enc = _auth_to_str(ap->authmode);

        double lat, lng;
        _gps_now(&lat, &lng);
        if (!_csv_fallback_append_wifi(bssid_str, (const char*)ap->ssid,
                                       enc, ap->primary, ap->rssi, lat, lng)) {
            _queue_csv_pause_log();
            break;
        }
    }
    pm_psram_free(records);
    _reset_ble_window();
}

static void _scan_worker_task(void* arg) {
    (void)arg;
    pm_log_i("WARDRIVE", "scan worker started");
    while (true) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        if (!s_running) continue;

        _poll_external_ble();
        if (!notified) continue;

        _process_scan_done();
        if (!s_running) continue;

        uint32_t waited = 0;
        while (s_running && waited < WD_SCAN_RESTART_DELAY_MS) {
            _poll_external_ble();
            pm_delay_ms(250);
            waited += 250;
        }
        if (s_running) {
            esp_err_t err = _start_wifi_scan();
            if (err != ESP_OK) {
                s_running = false;
                pm_wifi_scan_give(WD_WIFI_SCAN_OWNER);
                _stop_ble_source();
                _queue_status_log("SCAN", "ERR", esp_err_to_name(err), 0xff3366);
                s_scan_visual_stop_pending = true;
            }
        }
    }
}

static void _ensure_scan_worker(void) {
    _ensure_scan_ui_mutex();
    if (s_scan_worker) return;
    BaseType_t ok = xTaskCreatePinnedToCore(_scan_worker_task,
                                            "wd_scan",
                                            12288,
                                            NULL,
                                            4,
                                            &s_scan_worker,
                                            0);
    if (ok != pdPASS) {
        s_scan_worker = NULL;
        pm_log_w("WARDRIVE", "scan worker create failed");
    }
}

// Keep the system event-loop task tiny; the worker does the heavy lifting.
static void _wifi_event_handler(void* arg, esp_event_base_t base,
                                 int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;
    if (!pm_wifi_scan_is_owner(WD_WIFI_SCAN_OWNER)) return;
    if (!s_running || !s_scan_worker) return;
    xTaskNotifyGive(s_scan_worker);
}

static bool s_wifi_evt_registered = false;
static void _register_wifi_events(void) {
    if (s_wifi_evt_registered) return;
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                               _wifi_event_handler, NULL);
    if (err == ESP_OK) {
        s_wifi_evt_registered = true;
    } else {
        pm_log_w("WARDRIVE", "wifi event register failed: %s", esp_err_to_name(err));
    }
}

static void _start_cb(lv_event_t* e) {
    (void)e;
    if (s_running) return;
    if (!pm_wifi_scan_take(WD_WIFI_SCAN_OWNER, 0)) {
        char msg[80];
        snprintf(msg, sizeof(msg), "scanner busy: %s", pm_wifi_scan_owner());
        pm_log_w(TAG, "%s", msg);
        _queue_status_log("SCAN", "BUSY", msg, 0xffcc00);
        return;
    }
    _reset_capture_counters();
    s_running = true;
    pm_log_i("WARDRIVE", "scan start");
    _queue_status_log("SCAN", "SYS", "starting WiFi scan", 0x00ff88);

    // Lazy session init (deferred from _enter)
    _ensure_db();

    _ensure_scan_worker();
    if (!s_scan_worker) {
        s_running = false;
        pm_wifi_scan_give(WD_WIFI_SCAN_OWNER);
        _queue_status_log("SCAN", "ERR", "scan worker create failed",
                          0xff3366);
        return;
    }

    _register_wifi_events();

    // Trigger WiFi scan via ESP-Hosted (C6)
    esp_err_t err = _start_wifi_scan();
    if (err != ESP_OK) {
        s_running = false;
        pm_wifi_scan_give(WD_WIFI_SCAN_OWNER);
        pm_log_w("WARDRIVE", "scan_start failed: %s", esp_err_to_name(err));
        _queue_status_log("SCAN", "ERR", esp_err_to_name(err), 0xff3366);
        if (s_rec_dot) {
            lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(0x2a5870), 0);
        }
        if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, 25, 0);
        return;
    }

    _start_ble_source();

    if (s_rec_dot) lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(0xff3366), 0);
    if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, 80, 0);
}

static void _stop_cb(lv_event_t* e) {
    (void)e;
    if (!s_running) return;
    s_running = false;
    pm_log_i("WARDRIVE", "scan stop");
    if (s_scan_worker) xTaskNotifyGive(s_scan_worker);
    esp_wifi_scan_stop();
    pm_wifi_scan_give(WD_WIFI_SCAN_OWNER);
    if (s_rec_dot) {
        lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(0x2a5870), 0);
    }
    if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, 25, 0);
    _stop_ble_source();
}

static lv_obj_t* _make_stat_block(lv_obj_t* parent,
                                   const char* label_text,
                                   lv_color_t  accent) {
    lv_obj_t* blk = lv_obj_create(parent);
    lv_obj_remove_style_all(blk);
    lv_obj_set_style_bg_color(blk, lv_color_hex(0x080f14), 0);
    lv_obj_set_style_bg_opa(blk, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(blk, lv_color_hex(0x1a3a50), 0);
    lv_obj_set_style_border_width(blk, 1, 0);
    lv_obj_set_style_border_side(blk, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_flex_grow(blk, 1);
    lv_obj_set_height(blk, WD_STAT_H);
    lv_obj_set_layout(blk, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(blk, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(blk, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(blk, WD_LAYOUT_COMPACT ? 5 : 8, 0);
    lv_obj_t* num = lv_label_create(blk);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_font(num, WD_FONT_STAT, 0);
    lv_obj_set_style_text_color(num, accent, 0);
    lv_obj_t* lbl = lv_label_create(blk);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_letter_space(lbl, WD_ACTION_TEXT_SPACE, 0);
    return num;
}

static lv_obj_t* _make_chip(lv_obj_t* parent, const char* text,
                              lv_color_t color) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_set_style_bg_opa(chip, 30, 0);
    lv_obj_set_style_border_color(chip, color, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_pad_hor(chip, WD_LAYOUT_COMPACT ? 6 : 8, 0);
    lv_obj_set_style_pad_ver(chip, WD_LAYOUT_COMPACT ? 2 : 3, 0);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_t* lbl = lv_label_create(chip);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, WD_ACTION_TEXT_SPACE, 0);
    return chip;
}

static lv_obj_t* _make_action_btn(lv_obj_t* parent, const char* label,
                                   lv_color_t color, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, 25, 0);
    lv_obj_set_style_border_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_pad_hor(btn, WD_ACTION_BTN_PAD_H, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_set_height(btn, WD_ACTION_BTN_H);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_opa(btn, 80, LV_STATE_PRESSED);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, WD_FONT_TEXT, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, WD_ACTION_TEXT_SPACE, 0);
    lv_obj_center(lbl);
    return btn;
}

void pm_app_wardrive_add_network(const char* ssid, const char* bssid,
                                  int rssi, int channel, const char* enc) {
    if (!s_net_list) return;

    lv_obj_t* item = lv_obj_create(s_net_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_layout(item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(item, 8, 0);
    lv_obj_set_style_pad_left(item, 12, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0x1a3a50), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t* ssid_lbl = lv_label_create(item);
    lv_label_set_text(ssid_lbl, (ssid && ssid[0]) ? ssid : "(hidden)");
    lv_obj_set_style_text_font(ssid_lbl, WD_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xc8e8f5), 0);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ssid_lbl, LV_PCT(100));

    char meta[80];
    snprintf(meta, sizeof(meta), "%s  CH%d  %ddBm  %s",
             bssid ? bssid : "--:--:--:--:--:--",
             channel, rssi, enc ? enc : "OPEN");
    lv_obj_t* meta_lbl = lv_label_create(item);
    lv_label_set_text(meta_lbl, meta);
    lv_obj_set_style_text_font(meta_lbl, WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(meta_lbl, lv_color_hex(0x2a5870), 0);
    lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(meta_lbl, LV_PCT(100));
    _trim_children(s_net_list, WD_MAX_UI_NETWORK_ROWS);
}

void pm_app_wardrive_log(const char* timestamp, const char* type,
                          const char* content, lv_color_t color) {
    if (!s_live_log) return;
    wd_ui_log_t log = {0};
    _copy_text(log.timestamp, sizeof(log.timestamp), timestamp);
    _copy_text(log.type, sizeof(log.type), type);
    _copy_text(log.content, sizeof(log.content), content);
    log.color_hex = lv_color_to_u32(color) & 0x00ffffff;
    _append_display_log(&log);
    _update_log_rows();
}


// ── Map control callbacks (center panel) ────────────────────
// Resize the tilemap to the center panel's true pixel size once
// the flex layout has computed it (and on any later change).
static void _center_size_cb(lv_event_t* e) {
    lv_obj_t* center = lv_event_get_target(e);
    if (!s_map || !center) return;
    int w = lv_obj_get_content_width(center);
    int h = lv_obj_get_content_height(center);
    if (w > 16 && h > 16) pm_tilemap_resize(s_map, w, h);
}

static void _zoom_in_cb(lv_event_t* e) {
    (void)e;
    if (s_map) pm_tilemap_set_zoom(s_map, pm_tilemap_get_zoom(s_map) + 1);
}

static void _zoom_out_cb(lv_event_t* e) {
    (void)e;
    if (s_map) pm_tilemap_set_zoom(s_map, pm_tilemap_get_zoom(s_map) - 1);
}

// ── P4 redesign color constants ──────────────────────────────
#define WD_C_WIFI       lv_color_hex(0x00d4ff)
#define WD_C_BLE        lv_color_hex(0x00ff88)
#define WD_C_PROBE      lv_color_hex(0xf4a820)
#define WD_C_PKT        lv_color_hex(0xff3366)
#define WD_C_BG         lv_color_hex(0x050a0e)
#define WD_C_BG2        lv_color_hex(0x080f14)
#define WD_C_BG3        lv_color_hex(0x0c1620)
#define WD_C_BORDER     lv_color_hex(0x1a3a50)
#define WD_C_FG         lv_color_hex(0x7ab8d4)
#define WD_C_FG_BRIGHT  lv_color_hex(0xc8e8f5)
#define WD_C_FG_DIM     lv_color_hex(0x2a5870)

static void _build_screen(void) {
    pm_log_i(TAG, "build %s Wardrive layout for %s",
             WD_LAYOUT_COMPACT ? "5-inch compact" : "7-inch full",
             PM_BOARD_PANEL_DETAIL);

    // Root screen
    s_wd_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wd_screen, WD_C_BG, 0);
    lv_obj_set_style_bg_opa(s_wd_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wd_screen, 0, 0);
    lv_obj_set_layout(s_wd_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_wd_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(s_wd_screen, LV_PCT(100), LV_PCT(100));

    // ── 1. TITLEBAR ──────────────────────────────────────────
    lv_obj_t* titlebar = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_width(titlebar, LV_PCT(100));
    lv_obj_set_height(titlebar, WD_TITLEBAR_H);
    lv_obj_set_style_bg_color(titlebar, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(titlebar, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(titlebar, 1, 0);
    lv_obj_set_style_border_side(titlebar,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(titlebar, WD_TITLE_PAD_H, 0);
    lv_obj_set_style_pad_ver(titlebar, 0, 0);
    lv_obj_set_layout(titlebar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(titlebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(titlebar, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(titlebar, WD_TITLE_GAP, 0);

    // Back button
    lv_obj_t* back = lv_btn_create(titlebar);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, WD_BACK_SIZE, WD_BACK_SIZE);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_color(back_lbl,
        lv_color_hex(0x7ab8d4), LV_STATE_PRESSED);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back,
        _back_cb,
        LV_EVENT_CLICKED, NULL);

    // Title
    lv_obj_t* title = lv_label_create(titlebar);
    lv_label_set_text(title, "WARDRIVE");
    lv_obj_set_style_text_font(title, WD_FONT_TEXT, 0);
    lv_obj_set_style_text_color(title,
        lv_color_hex(0xc8e8f5), 0);
    lv_obj_set_style_text_letter_space(title, WD_ACTION_TEXT_SPACE, 0);

    // Spacer
    lv_obj_t* sp1 = lv_obj_create(titlebar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    // Session label (truncated)
    s_lbl_session = lv_label_create(titlebar);
    lv_label_set_text(s_lbl_session, "session_pending");
    lv_obj_set_style_text_font(s_lbl_session,
        WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_lbl_session,
        lv_color_hex(0x4a7a92), 0);
    lv_obj_set_width(s_lbl_session, WD_SESSION_W);
    lv_label_set_long_mode(s_lbl_session, LV_LABEL_LONG_CLIP);

    // C6 Ghost chip
    _make_chip(titlebar, "C6 GHOST",
               lv_color_hex(0x00ff88));

    // GPS chip (will update when GPS locks)
    s_chip_gps = _make_chip(titlebar, "GPS?",
                             lv_color_hex(0xffcc00));

    // DB chip (SQLite or CSV)
    s_chip_db = _make_chip(titlebar, "SQLITE",
                            lv_color_hex(0x00d4ff));

    // Recording dot
    s_rec_dot = lv_obj_create(titlebar);
    lv_obj_remove_style_all(s_rec_dot);
    lv_obj_set_size(s_rec_dot, WD_REC_DOT_SIZE, WD_REC_DOT_SIZE);
    lv_obj_set_style_radius(s_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_rec_dot,
        lv_color_hex(0x2a5870), 0);   // dim = not recording
    lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);

    // ── 2. STATS ROW ─────────────────────────────────────────
    lv_obj_t* stats_row = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(stats_row);
    lv_obj_set_width(stats_row, LV_PCT(100));
    lv_obj_set_height(stats_row, WD_STATS_H);
    lv_obj_set_style_bg_color(stats_row, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stats_row, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(stats_row, 1, 0);
    lv_obj_set_style_border_side(stats_row,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_layout(stats_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_wifi  = _make_stat_block(stats_row, "WIFI",   WD_C_WIFI);
    s_lbl_ble   = _make_stat_block(stats_row, "BLE",    WD_C_BLE);
    s_lbl_probe = _make_stat_block(stats_row, "PROBES", WD_C_PROBE);
    s_lbl_pkt   = _make_stat_block(stats_row, "PACKETS",WD_C_PKT);

    // ── 3. MAIN CONTENT AREA (3 columns) ─────────────────────
    // Full: 1024x600 dashboard. Compact: 800x480 with narrower panels.
    lv_obj_t* content = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, 0, 0);

    // 3a. LEFT PANEL — network list
    lv_obj_t* left = lv_obj_create(content);
    lv_obj_remove_style_all(left);
    lv_obj_set_width(left, WD_LEFT_W);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_color(left, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    // Left header
    lv_obj_t* left_hdr = lv_obj_create(left);
    lv_obj_remove_style_all(left_hdr);
    lv_obj_set_width(left_hdr, LV_PCT(100));
    lv_obj_set_height(left_hdr, WD_PANEL_HEADER_H);
    lv_obj_set_style_bg_color(left_hdr, WD_C_BG3, 0);
    lv_obj_set_style_bg_opa(left_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left_hdr, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(left_hdr, 1, 0);
    lv_obj_set_style_border_side(left_hdr,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(left_hdr, 10, 0);
    lv_obj_t* left_hdr_lbl = lv_label_create(left_hdr);
    lv_label_set_text(left_hdr_lbl, "NETWORKS");
    lv_obj_set_style_text_font(left_hdr_lbl,
        WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(left_hdr_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_letter_space(left_hdr_lbl, WD_ACTION_TEXT_SPACE, 0);
    lv_obj_align(left_hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Network list (scrollable)
    s_net_list = lv_obj_create(left);
    lv_obj_remove_style_all(s_net_list);
    lv_obj_set_width(s_net_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_net_list, 1);
    lv_obj_set_layout(s_net_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_net_list, LV_DIR_VER);
    lv_obj_set_style_pad_all(s_net_list, 0, 0);

    // 3b. CENTER PANEL — slippy-map renderer with GPS overlay
    lv_obj_t* center = lv_obj_create(content);
    lv_obj_remove_style_all(center);
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_height(center, LV_PCT(100));
    lv_obj_set_style_bg_color(center, WD_C_BG, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    s_map_center = center;

    // Tile renderer fills the panel (drawn behind the overlays).
    // Initial size is an estimate; _center_size_cb snaps it to the
    // panel's real dimensions once flex layout settles. Tiles load
    // from /sd/tiles; with no tile pack present the map is simply a
    // dark panel and the GPS overlay carries the information.
    int map_w0 = WD_LAYOUT_COMPACT ? 250 : 404;
    int map_h0 = WD_LAYOUT_COMPACT ? 330 : 404;
    s_map = pm_tilemap_create(center, map_w0, map_h0);
    if (s_map) {
        pm_tilemap_set_zoom(s_map, WD_MAP_DEFAULT_ZOOM);
        pm_tilemap_set_center(s_map, WD_MAP_DEFAULT_LAT, WD_MAP_DEFAULT_LON);
        lv_obj_set_pos(pm_tilemap_obj(s_map), 0, 0);
    } else {
        pm_log_w(TAG, "tilemap create failed — center panel text only");
    }
    lv_obj_add_event_cb(center, _center_size_cb, LV_EVENT_SIZE_CHANGED, NULL);

    // GPS status overlay — floats top-left over the map. This is
    // the same label the render path writes fix text into, so the
    // existing GPS-text logic in _render() needs no change.
    lv_obj_t* gps_overlay = lv_label_create(center);
    lv_label_set_text(gps_overlay, WD_CENTER_TEXT);
    lv_obj_set_style_text_font(gps_overlay, WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(gps_overlay, lv_color_hex(0xc8e8f5), 0);
    lv_obj_set_style_bg_color(gps_overlay, WD_C_BG, 0);
    lv_obj_set_style_bg_opa(gps_overlay, 170, 0);   // semi-transparent
    lv_obj_set_style_pad_all(gps_overlay, 6, 0);
    lv_obj_set_style_radius(gps_overlay, 4, 0);
    lv_obj_set_style_border_color(gps_overlay, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(gps_overlay, 1, 0);
    lv_obj_align(gps_overlay, LV_ALIGN_TOP_LEFT, 8, 8);
    s_center_canvas = gps_overlay;

    // Zoom controls — float top-right over the map.
    lv_obj_t* zoom_in = lv_btn_create(center);
    lv_obj_remove_style_all(zoom_in);
    lv_obj_set_size(zoom_in, 30, 30);
    lv_obj_set_style_bg_color(zoom_in, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(zoom_in, 200, 0);
    lv_obj_set_style_border_color(zoom_in, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(zoom_in, 1, 0);
    lv_obj_set_style_radius(zoom_in, 4, 0);
    lv_obj_align(zoom_in, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(zoom_in, _zoom_in_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* zi_lbl = lv_label_create(zoom_in);
    lv_label_set_text(zi_lbl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(zi_lbl, lv_color_hex(0xc8e8f5), 0);
    lv_obj_center(zi_lbl);

    lv_obj_t* zoom_out = lv_btn_create(center);
    lv_obj_remove_style_all(zoom_out);
    lv_obj_set_size(zoom_out, 30, 30);
    lv_obj_set_style_bg_color(zoom_out, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(zoom_out, 200, 0);
    lv_obj_set_style_border_color(zoom_out, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(zoom_out, 1, 0);
    lv_obj_set_style_radius(zoom_out, 4, 0);
    lv_obj_align(zoom_out, LV_ALIGN_TOP_RIGHT, -8, 44);
    lv_obj_add_event_cb(zoom_out, _zoom_out_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* zo_lbl = lv_label_create(zoom_out);
    lv_label_set_text(zo_lbl, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(zo_lbl, lv_color_hex(0xc8e8f5), 0);
    lv_obj_center(zo_lbl);

    // 3c. RIGHT PANEL — live log
    lv_obj_t* right = lv_obj_create(content);
    lv_obj_remove_style_all(right);
    lv_obj_set_width(right, WD_RIGHT_W);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_color(right, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);

    // Right header
    lv_obj_t* right_hdr = lv_obj_create(right);
    lv_obj_remove_style_all(right_hdr);
    lv_obj_set_width(right_hdr, LV_PCT(100));
    lv_obj_set_height(right_hdr, WD_PANEL_HEADER_H);
    lv_obj_set_style_bg_color(right_hdr, WD_C_BG3, 0);
    lv_obj_set_style_bg_opa(right_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right_hdr, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(right_hdr, 1, 0);
    lv_obj_set_style_border_side(right_hdr,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(right_hdr, 10, 0);
    lv_obj_t* right_hdr_lbl = lv_label_create(right_hdr);
    lv_label_set_text(right_hdr_lbl, "LIVE CAPTURE");
    lv_obj_set_style_text_font(right_hdr_lbl,
        WD_FONT_LABEL, 0);
    lv_obj_set_style_text_color(right_hdr_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_letter_space(right_hdr_lbl, WD_ACTION_TEXT_SPACE, 0);
    lv_obj_align(right_hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Live log (scrollable)
    s_live_log = lv_obj_create(right);
    lv_obj_remove_style_all(s_live_log);
    lv_obj_set_width(s_live_log, LV_PCT(100));
    lv_obj_set_flex_grow(s_live_log, 1);
    lv_obj_set_layout(s_live_log, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_live_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_live_log, LV_DIR_VER);
    lv_obj_set_style_pad_all(s_live_log, 0, 0);

    // ── 4. ACTION BAR ─────────────────────────────────────────
    lv_obj_t* action_bar = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(action_bar);
    lv_obj_set_width(action_bar, LV_PCT(100));
    lv_obj_set_height(action_bar, WD_ACTION_H);
    lv_obj_set_style_bg_color(action_bar, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(action_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(action_bar, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(action_bar, 1, 0);
    lv_obj_set_style_border_side(action_bar,
        LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_layout(action_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(action_bar, WD_ACTION_PAD_H, 0);
    lv_obj_set_style_pad_column(action_bar, WD_ACTION_GAP, 0);

    s_btn_start = _make_action_btn(action_bar, LV_SYMBOL_PLAY " START",
                                    lv_color_hex(0x00ff88), _start_cb);
    s_btn_stop  = _make_action_btn(action_bar, LV_SYMBOL_STOP " STOP",
                                    lv_color_hex(0xff3366), _stop_cb);
    _make_action_btn(action_bar, LV_SYMBOL_UPLOAD " EXPORT",
                     lv_color_hex(0x00d4ff), _export_cb);
    _make_action_btn(action_bar, LV_SYMBOL_GPS " GPS",
                     lv_color_hex(0xf4a820),
                     _map_cb);
    _make_action_btn(action_bar, LV_SYMBOL_SETTINGS,
                     lv_color_hex(0x4a7a92),
                     _settings_cb);
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    s_foreground = true;
    if (!s_wd_screen) {
        _build_screen();   // lazy: build UI only when user opens the app
    }
    if (s_wd_screen) lv_screen_load(s_wd_screen);
    pm_log_i(TAG, "enter");
    // DB deferred to scan start
    _render();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (!s_foreground) return;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 500) return;
    s_last_render_ms = now;
    if (!lvgl_port_lock(0)) return;
    _render();
    lvgl_port_unlock();
}

static void _exit_(void) {
    s_foreground = false;
    pm_log_i(TAG, "exit foreground (running=%d)", (int)s_running);
}

static void _deinit(void) {
    if (s_running) {
        s_running = false;
        if (s_scan_worker) xTaskNotifyGive(s_scan_worker);
        esp_wifi_scan_stop();
        pm_wifi_scan_give(WD_WIFI_SCAN_OWNER);
    }
    _stop_ble_source();
    if (s_map) {
        pm_tilemap_destroy(s_map);   // frees tile PSRAM buffers + LVGL objs
        s_map = NULL;
        s_map_center = NULL;
        s_center_canvas = NULL;
        s_track_n = 0;
        s_have_last_track = false;
    }
    if (s_db) { pm_db_close(s_db); s_db = NULL; }
}

static const pm_app_t _APP = {
    .id           = "wardrive",
    .display_name = "WARDRIVE",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_wardrive(void) { return &_APP; }
