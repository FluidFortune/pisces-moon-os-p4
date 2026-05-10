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
#include "pm_ui.h"
#include "pm_sqlite.h"
#include "pm_gps_state.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_WARDRIVE";

#define SESSIONS_DIR "/sd/sessions"
#define EXPORTS_DIR  "/sd/exports"

static pm_db_t* s_db = NULL;
static char     s_session_path[80] = "";
static char     s_session_label[40] = "";
static bool     s_csv_fallback = false;

// HUD counters
static int s_wifi_total = 0;
static int s_ble_total  = 0;
static int s_probe_total = 0;
static int s_pkt_total  = 0;

// LVGL handles
static void* s_screen = NULL;

// ─────────────────────────────────────────────
//  Lazy open
// ─────────────────────────────────────────────
static bool _ensure_db(void) {
    if (s_db) return true;
    if (s_csv_fallback) return false;

    pm_file_mkdir(SESSIONS_DIR);
    pm_file_mkdir(EXPORTS_DIR);

    // Pick session label from millis (real timestamp once GPS or
    // RTC is online — pm_time_now()).
    uint32_t up = pm_uptime_seconds();
    snprintf(s_session_label, sizeof(s_session_label), "%010u", (unsigned)up);
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

static void _csv_fallback_append_wifi(const char* bssid, const char* ssid,
                                       const char* enc, int channel, int rssi,
                                       double lat, double lng) {
    pm_file_mkdir(EXPORTS_DIR);
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
    PM_SPI_TAKE("wd_csv_wifi") {
        pm_file_t* f = pm_file_open(path, PM_FILE_APPEND | PM_FILE_CREATE);
        if (f) { pm_file_write(f, line, n); pm_file_close(f); }
    } PM_SPI_GIVE();
}

void pm_app_wardrive_on_wifi(const char* bssid, const char* ssid,
                              int rssi, int channel, const char* enc) {
    if (!bssid) return;
    s_wifi_total++;
    double lat, lng; _gps_now(&lat, &lng);
    uint32_t now = pm_millis();

    if (!_ensure_db()) {
        _csv_fallback_append_wifi(bssid, ssid, enc, channel, rssi, lat, lng);
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
    if (!mac) return;
    s_ble_total++;
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
static lv_obj_t* s_lbl_mode_chip = NULL;
static lv_obj_t* s_lbl_wifi  = NULL;
static lv_obj_t* s_lbl_ble   = NULL;
static lv_obj_t* s_lbl_probe = NULL;
static lv_obj_t* s_lbl_pkt   = NULL;

static void _render(void) {
    if (s_lbl_session) {
        char buf[48];
        snprintf(buf, sizeof(buf), "session_%s%s",
                  s_session_label,
                  s_csv_fallback ? " (CSV fallback)" : "");
        lv_label_set_text(s_lbl_session, buf);
    }
    char nbuf[16];
    if (s_lbl_wifi)  { snprintf(nbuf, sizeof(nbuf), "%d", s_wifi_total);  lv_label_set_text(s_lbl_wifi,  nbuf); }
    if (s_lbl_ble)   { snprintf(nbuf, sizeof(nbuf), "%d", s_ble_total);   lv_label_set_text(s_lbl_ble,   nbuf); }
    if (s_lbl_probe) { snprintf(nbuf, sizeof(nbuf), "%d", s_probe_total); lv_label_set_text(s_lbl_probe, nbuf); }
    if (s_lbl_pkt)   { snprintf(nbuf, sizeof(nbuf), "%d", s_pkt_total);   lv_label_set_text(s_lbl_pkt,   nbuf); }
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_wd_screen = NULL;

static void _export_cb(lv_event_t* e) {
    (void)e;
    pm_app_wardrive_export_csv();
}

static lv_obj_t* _stat(lv_obj_t* parent, const char* label, lv_color_t accent) {
    lv_obj_t* card = pm_ui_card(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* num = lv_label_create(card);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_color(num, accent, 0);
    lv_obj_set_style_text_font (num, &lv_font_montserrat_28, 0);
    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, label);
    lv_obj_set_style_text_color(lab, PM_C_FG_DIM, 0);
    return num;
}

static void _build_screen(void) {
    s_wd_screen = pm_ui_screen();
    pm_ui_titlebar(s_wd_screen, "WARDRIVE", NULL, NULL);

    // Header card
    lv_obj_t* hdr = pm_ui_card(s_wd_screen);
    lv_obj_set_height(hdr, 70);
    s_lbl_session = lv_label_create(hdr);
    lv_label_set_text(s_lbl_session, "(session pending)");
    lv_obj_set_style_text_color(s_lbl_session, PM_C_ACCENT_2, 0);

    lv_obj_t* chips = lv_obj_create(hdr);
    lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(chips, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, 6, 0);
    pm_ui_chip(chips, "C6 Ghost",  PM_C_OK);
    pm_ui_chip(chips, "GPS lock?", PM_C_WARN);
    pm_ui_chip(chips, "SQLite",    PM_C_ACCENT);

    // Stats row — 4 numeric cards
    lv_obj_t* stats = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), 100);
    lv_obj_set_layout(stats, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stats, 8, 0);
    s_lbl_wifi  = _stat(stats, "WIFI",   PM_C_ACCENT);
    s_lbl_ble   = _stat(stats, "BLE",    PM_C_ACCENT_2);
    s_lbl_probe = _stat(stats, "PROBES", PM_C_WARN);
    s_lbl_pkt   = _stat(stats, "PACKETS",PM_C_OK);

    // Buttons
    lv_obj_t* btn_row = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    pm_ui_button(btn_row, "Export CSV", _export_cb, NULL);
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_wd_screen) lv_screen_load(s_wd_screen);
    pm_log_i(TAG, "enter");
    _ensure_db();
    _render();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_render_ms < 500) return;
    s_last_render_ms = now;
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
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
