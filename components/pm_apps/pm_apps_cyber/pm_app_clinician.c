// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_clinician.c - 7" native RF analysis workstation
//
//  The WebOS Clinician remains the full spreadsheet/XLSX/map
//  workstation. This app is a firmware-native field analyst:
//  capture a C6 WiFi snapshot or load the latest SD CSV, then
//  compute risk, channel pressure, GPS coverage, and findings.
// ============================================================

#include "pm_app_clinician.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pm_gps_state.h"
#include "pm_hal.h"
#include "pm_launcher.h"
#include "pm_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "PM_CLIN";

#define CL_WIFI_SCAN_OWNER "clinician"
#define CL_MAX_RECORDS        320
#define CL_MAX_SCAN_RECORDS    96
#define CL_VISIBLE_ROWS        10
#define CL_EXPORTS_DIR        "/sd/exports"
#define CL_REPORT_DIR         "/sd/reports"

typedef struct {
    char bssid[18];
    char ssid[33];
    char enc[12];
    int  rssi;
    int  channel;
    int  hits;
    bool gps_valid;
    double lat;
    double lng;
} cl_record_t;

typedef struct {
    int records;
    int open;
    int wep;
    int hidden;
    int gps;
    int strong;
    int avg_rssi;
    int busiest_channel;
    int busiest_hits;
    int risk_score;
    char risk_label[8];
} cl_stats_t;

static cl_record_t* s_records = NULL;
static cl_record_t* s_render_records = NULL;
static int s_record_count = 0;
static int s_channel_hits[14];
static cl_stats_t s_stats;
static char s_source_label[96] = "no dataset loaded";
static char s_status_line[96] = "ready";

static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_scan_worker = NULL;
static bool s_wifi_evt_registered = false;
static volatile bool s_capture_pending = false;
static volatile bool s_dirty = true;

static lv_obj_t* s_screen = NULL;
static lv_obj_t* s_lbl_source = NULL;
static lv_obj_t* s_lbl_status = NULL;
static lv_obj_t* s_lbl_records = NULL;
static lv_obj_t* s_lbl_risk = NULL;
static lv_obj_t* s_lbl_open = NULL;
static lv_obj_t* s_lbl_gps = NULL;
static lv_obj_t* s_lbl_avg = NULL;
static lv_obj_t* s_lbl_channel = NULL;
static lv_obj_t* s_report = NULL;
static lv_obj_t* s_table = NULL;
static lv_obj_t* s_channel_bars[14];

static bool _ensure_state(void) {
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return false;
    }
    if (!s_records) {
        s_records = (cl_record_t*)pm_psram_calloc(CL_MAX_RECORDS,
                                                  sizeof(cl_record_t));
        if (!s_records) {
            s_records = (cl_record_t*)calloc(CL_MAX_RECORDS,
                                             sizeof(cl_record_t));
        }
        if (!s_records) return false;
    }
    if (!s_render_records) {
        s_render_records = (cl_record_t*)pm_psram_calloc(CL_MAX_RECORDS,
                                                         sizeof(cl_record_t));
        if (!s_render_records) {
            s_render_records = (cl_record_t*)calloc(CL_MAX_RECORDS,
                                                    sizeof(cl_record_t));
        }
        if (!s_render_records) return false;
    }
    return true;
}

static void _copy_text(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static void _format_bssid(const uint8_t bssid[6], char* out, size_t out_len) {
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

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

static void _clear_locked(void) {
    memset(s_records, 0, CL_MAX_RECORDS * sizeof(s_records[0]));
    memset(s_channel_hits, 0, sizeof(s_channel_hits));
    memset(&s_stats, 0, sizeof(s_stats));
    _copy_text(s_stats.risk_label, sizeof(s_stats.risk_label), "LOW");
    s_record_count = 0;
}

static int _find_or_make_locked(const char* bssid) {
    for (int i = 0; i < s_record_count; i++) {
        if (strcmp(s_records[i].bssid, bssid) == 0) return i;
    }
    if (s_record_count < CL_MAX_RECORDS) return s_record_count++;

    int weakest = 0;
    for (int i = 1; i < s_record_count; i++) {
        if (s_records[i].rssi < s_records[weakest].rssi) weakest = i;
    }
    memset(&s_records[weakest], 0, sizeof(s_records[weakest]));
    return weakest;
}

static void _record_locked(const char* bssid, const char* ssid,
                           const char* enc, int channel, int rssi,
                           int hits, double lat, double lng) {
    if (!bssid || !bssid[0]) return;
    int idx = _find_or_make_locked(bssid);
    cl_record_t* r = &s_records[idx];
    _copy_text(r->bssid, sizeof(r->bssid), bssid);
    _copy_text(r->ssid, sizeof(r->ssid), ssid);
    _copy_text(r->enc, sizeof(r->enc), enc && enc[0] ? enc : "WPA");
    r->channel = channel;
    r->rssi = rssi;
    r->hits += hits > 0 ? hits : 1;
    if (lat != 0.0 || lng != 0.0) {
        r->gps_valid = true;
        r->lat = lat;
        r->lng = lng;
    }
}

static void _analyze_locked(void) {
    memset(s_channel_hits, 0, sizeof(s_channel_hits));
    memset(&s_stats, 0, sizeof(s_stats));
    int rssi_sum = 0;
    int rssi_n = 0;

    s_stats.records = s_record_count;
    for (int i = 0; i < s_record_count; i++) {
        cl_record_t* r = &s_records[i];
        if (strcmp(r->enc, "OPEN") == 0) s_stats.open++;
        if (strcmp(r->enc, "WEP") == 0) s_stats.wep++;
        if (!r->ssid[0] || strcmp(r->ssid, "(hidden)") == 0) s_stats.hidden++;
        if (r->gps_valid) s_stats.gps++;
        if (r->rssi > -45) s_stats.strong++;
        if (r->rssi < 0) {
            rssi_sum += r->rssi;
            rssi_n++;
        }
        if (r->channel >= 1 && r->channel <= 14) {
            s_channel_hits[r->channel - 1] += r->hits > 0 ? r->hits : 1;
        }
    }

    s_stats.avg_rssi = rssi_n ? rssi_sum / rssi_n : 0;
    for (int i = 0; i < 14; i++) {
        if (s_channel_hits[i] > s_stats.busiest_hits) {
            s_stats.busiest_hits = s_channel_hits[i];
            s_stats.busiest_channel = i + 1;
        }
    }

    s_stats.risk_score =
        (s_stats.open * 8) +
        (s_stats.wep * 10) +
        (s_stats.hidden * 2) +
        (s_stats.strong * 2) +
        (s_stats.busiest_hits > 12 ? 8 : 0);

    if (s_stats.risk_score >= 40) {
        _copy_text(s_stats.risk_label, sizeof(s_stats.risk_label), "HIGH");
    } else if (s_stats.risk_score >= 15) {
        _copy_text(s_stats.risk_label, sizeof(s_stats.risk_label), "MED");
    } else {
        _copy_text(s_stats.risk_label, sizeof(s_stats.risk_label), "LOW");
    }
}

static void _record_ap_locked(const wifi_ap_record_t* ap) {
    char bssid[18];
    _format_bssid(ap->bssid, bssid, sizeof(bssid));
    pm_gps_t g;
    pm_gps_state_get(&g);
    _record_locked(bssid, (const char*)ap->ssid, _auth_to_str(ap->authmode),
                   ap->primary, ap->rssi, 1,
                   g.valid ? g.lat : 0.0,
                   g.valid ? g.lng : 0.0);
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
    return esp_wifi_scan_start(&cfg, false);
}

static void _process_scan_done(void) {
    if (!pm_wifi_scan_is_owner(CL_WIFI_SCAN_OWNER)) return;

    uint16_t found = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) {
        _copy_text(s_status_line, sizeof(s_status_line), esp_err_to_name(err));
        s_capture_pending = false;
        s_dirty = true;
        return;
    }

    uint16_t wanted = found;
    if (wanted > CL_MAX_SCAN_RECORDS) wanted = CL_MAX_SCAN_RECORDS;
    wifi_ap_record_t* aps = NULL;
    if (wanted > 0) {
        aps = (wifi_ap_record_t*)calloc(wanted, sizeof(wifi_ap_record_t));
        if (!aps) {
            esp_wifi_clear_ap_list();
            _copy_text(s_status_line, sizeof(s_status_line), "scan alloc failed");
            s_capture_pending = false;
            s_dirty = true;
            return;
        }
        uint16_t got = wanted;
        err = esp_wifi_scan_get_ap_records(&got, aps);
        if (err != ESP_OK) {
            free(aps);
            esp_wifi_clear_ap_list();
            _copy_text(s_status_line, sizeof(s_status_line), esp_err_to_name(err));
            s_capture_pending = false;
            s_dirty = true;
            return;
        }
        wanted = got;
    }
    esp_wifi_clear_ap_list();

    if (_ensure_state() && xSemaphoreTake(s_lock, pdMS_TO_TICKS(300)) == pdTRUE) {
        _clear_locked();
        for (uint16_t i = 0; i < wanted; i++) {
            _record_ap_locked(&aps[i]);
        }
        _analyze_locked();
        snprintf(s_source_label, sizeof(s_source_label),
                 "live C6 snapshot: %u of %u APs", (unsigned)wanted, (unsigned)found);
        snprintf(s_status_line, sizeof(s_status_line),
                 "analysis complete - %d records", s_record_count);
        xSemaphoreGive(s_lock);
    }

    free(aps);
    s_capture_pending = false;
    s_dirty = true;
}

static void _scan_worker_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "capture worker started");
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_capture_pending) continue;
        _process_scan_done();
        pm_wifi_scan_give(CL_WIFI_SCAN_OWNER);
    }
}

static void _wifi_event_handler(void* arg, esp_event_base_t base,
                                int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;
    if (!pm_wifi_scan_is_owner(CL_WIFI_SCAN_OWNER)) return;
    if (!s_capture_pending || !s_scan_worker) return;
    xTaskNotifyGive(s_scan_worker);
}

static void _register_wifi_events(void) {
    if (s_wifi_evt_registered) return;
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                               _wifi_event_handler, NULL);
    if (err == ESP_OK) {
        s_wifi_evt_registered = true;
    } else {
        pm_log_w(TAG, "wifi event register failed: %s", esp_err_to_name(err));
    }
}

static void _ensure_worker(void) {
    if (s_scan_worker) return;
    BaseType_t ok = xTaskCreatePinnedToCore(_scan_worker_task,
                                            "clin_scan",
                                            12288,
                                            NULL,
                                            4,
                                            &s_scan_worker,
                                            0);
    if (ok != pdPASS) {
        s_scan_worker = NULL;
        _copy_text(s_status_line, sizeof(s_status_line), "worker create failed");
    }
}

static int _csv_split(char* line, char* fields[], int max_fields) {
    int n = 0;
    bool quote = false;
    fields[n++] = line;
    for (char* p = line; *p && n < max_fields; p++) {
        if (*p == '"') {
            quote = !quote;
        } else if (*p == ',' && !quote) {
            *p = 0;
            fields[n++] = p + 1;
        } else if (*p == '\r' || *p == '\n') {
            *p = 0;
            break;
        }
    }
    for (int i = 0; i < n; i++) {
        if (fields[i][0] == '"') fields[i]++;
        size_t len = strlen(fields[i]);
        if (len > 0 && fields[i][len - 1] == '"') fields[i][len - 1] = 0;
    }
    return n;
}

static bool _looks_like_csv(const char* name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 5 || strcmp(name + n - 4, ".csv") != 0) return false;
    return strncmp(name, "wardrive_", 9) == 0 ||
           strncmp(name, "silas_", 6) == 0;
}

static bool _find_latest_csv(char* out, size_t out_len) {
    pm_dir_t* d = pm_dir_open(CL_EXPORTS_DIR);
    if (!d) return false;

    char best[64] = "";
    bool is_dir = false;
    const char* name = NULL;
    while ((name = pm_dir_next(d, &is_dir)) != NULL) {
        if (is_dir || !_looks_like_csv(name)) continue;
        if (!best[0] || strcmp(name, best) > 0) {
            _copy_text(best, sizeof(best), name);
        }
    }
    pm_dir_close(d);

    if (!best[0]) return false;
    snprintf(out, out_len, "%s/%s", CL_EXPORTS_DIR, best);
    return true;
}

static bool _load_csv_file(const char* path) {
    if (!path || !path[0]) return false;
    if (!_ensure_state()) return false;

    pm_file_t* f = NULL;
    PM_SPI_TAKE("clinician_csv") {
        f = pm_file_open(path, PM_FILE_READ);
    } PM_SPI_GIVE();
    if (!f) return false;

    bool ok = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(300)) == pdTRUE) {
        _clear_locked();

        char line[384];
        size_t pos = 0;
        char ch = 0;
        while (pm_file_read(f, &ch, 1) == 1) {
            if (ch == '\n' || pos >= sizeof(line) - 1) {
                line[pos] = 0;
                pos = 0;

                if (!line[0] ||
                    strncmp(line, "WigleWifi", 9) == 0 ||
                    strncmp(line, "MAC,", 4) == 0 ||
                    strncmp(line, "bssid,", 6) == 0) {
                    continue;
                }

                char* fields[16] = {0};
                int n = _csv_split(line, fields, 16);
                if (n < 6) continue;

                int ch_idx = 4;
                int rssi_idx = 5;
                int hits_idx = -1;
                int lat_idx = 6;
                int lng_idx = 7;
                if (n >= 9 && strcmp(fields[8], "c6_hosted") == 0) {
                    ch_idx = 3;
                    rssi_idx = 4;
                    hits_idx = 5;
                    lat_idx = 6;
                    lng_idx = 7;
                }

                int channel = ch_idx < n ? atoi(fields[ch_idx]) : 0;
                int rssi = rssi_idx < n ? atoi(fields[rssi_idx]) : 0;
                int hits = hits_idx >= 0 && hits_idx < n ? atoi(fields[hits_idx]) : 1;
                double lat = lat_idx < n ? strtod(fields[lat_idx], NULL) : 0.0;
                double lng = lng_idx < n ? strtod(fields[lng_idx], NULL) : 0.0;

                _record_locked(fields[0], fields[1], fields[2],
                               channel, rssi, hits, lat, lng);
                if (s_record_count >= CL_MAX_RECORDS) break;
            } else if (ch != '\r') {
                line[pos++] = ch;
            }
        }

        if (pos > 0 && s_record_count < CL_MAX_RECORDS) {
            line[pos] = 0;
            char* fields[16] = {0};
            int n = _csv_split(line, fields, 16);
            if (n >= 6 &&
                strncmp(line, "WigleWifi", 9) != 0 &&
                strncmp(line, "MAC,", 4) != 0 &&
                strncmp(line, "bssid,", 6) != 0) {
                _record_locked(fields[0], fields[1], fields[2],
                               atoi(fields[4]), atoi(fields[5]), 1,
                               n > 6 ? strtod(fields[6], NULL) : 0.0,
                               n > 7 ? strtod(fields[7], NULL) : 0.0);
            }
        }

        _analyze_locked();
        ok = s_record_count > 0;
        snprintf(s_source_label, sizeof(s_source_label), "loaded %s", path);
        snprintf(s_status_line, sizeof(s_status_line),
                 ok ? "analysis complete - %d records" : "no records parsed",
                 s_record_count);
        xSemaphoreGive(s_lock);
    }

    pm_file_close(f);
    s_dirty = true;
    return ok;
}

static void _capture_cb(lv_event_t* e) {
    (void)e;
    if (s_capture_pending) return;
    if (!_ensure_state()) return;
    if (!pm_wifi_scan_take(CL_WIFI_SCAN_OWNER, 0)) {
        snprintf(s_status_line, sizeof(s_status_line),
                 "scanner busy: %s", pm_wifi_scan_owner());
        s_dirty = true;
        return;
    }
    _ensure_worker();
    if (!s_scan_worker) {
        pm_wifi_scan_give(CL_WIFI_SCAN_OWNER);
        return;
    }
    _register_wifi_events();

    s_capture_pending = true;
    _copy_text(s_status_line, sizeof(s_status_line), "capturing C6 WiFi snapshot...");
    s_dirty = true;

    esp_err_t err = _start_wifi_scan();
    if (err != ESP_OK) {
        s_capture_pending = false;
        pm_wifi_scan_give(CL_WIFI_SCAN_OWNER);
        _copy_text(s_status_line, sizeof(s_status_line), esp_err_to_name(err));
        s_dirty = true;
    }
}

static void _load_cb(lv_event_t* e) {
    (void)e;
    if (!pm_sd_mounted()) {
        _copy_text(s_status_line, sizeof(s_status_line), "SD not mounted");
        s_dirty = true;
        return;
    }
    char path[96];
    if (!_find_latest_csv(path, sizeof(path))) {
        _copy_text(s_status_line, sizeof(s_status_line), "no CSV in /sd/exports");
        s_dirty = true;
        return;
    }
    if (!_load_csv_file(path)) {
        _copy_text(s_status_line, sizeof(s_status_line), "CSV load failed");
        s_dirty = true;
    }
}

static void _clear_cb(lv_event_t* e) {
    (void)e;
    if (!_ensure_state()) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        _clear_locked();
        _copy_text(s_source_label, sizeof(s_source_label), "dataset cleared");
        _copy_text(s_status_line, sizeof(s_status_line), "ready");
        xSemaphoreGive(s_lock);
    }
    s_dirty = true;
}

static void _report_cb(lv_event_t* e) {
    (void)e;
    if (!pm_sd_mounted()) {
        _copy_text(s_status_line, sizeof(s_status_line), "SD not mounted");
        s_dirty = true;
        return;
    }
    pm_file_mkdir(CL_REPORT_DIR);
    char path[96];
    snprintf(path, sizeof(path), "%s/clinician_%010u.txt",
             CL_REPORT_DIR, (unsigned)pm_uptime_seconds());

    bool ok = false;
    PM_SPI_TAKE("clinician_report") {
        pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
                pm_file_printf(f, "Pisces Moon The Clinician - Native Field Report\n");
                pm_file_printf(f, "Source: %s\n", s_source_label);
                pm_file_printf(f, "Risk: %s (%d)\n", s_stats.risk_label, s_stats.risk_score);
                pm_file_printf(f, "Records: %d\nOpen: %d\nWEP: %d\nHidden: %d\nGPS tagged: %d\n",
                               s_stats.records, s_stats.open, s_stats.wep,
                               s_stats.hidden, s_stats.gps);
                pm_file_printf(f, "Busiest channel: %d (%d observations)\n",
                               s_stats.busiest_channel, s_stats.busiest_hits);
                xSemaphoreGive(s_lock);
                ok = true;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    snprintf(s_status_line, sizeof(s_status_line), ok ? "report written" : "report failed");
    s_dirty = true;
}

static void _back_cb(lv_event_t* e) {
    (void)e;
    s_capture_pending = false;
    if (pm_wifi_scan_is_owner(CL_WIFI_SCAN_OWNER)) {
        esp_wifi_scan_stop();
        pm_wifi_scan_give(CL_WIFI_SCAN_OWNER);
    }
    pm_launcher_show();
}

static lv_obj_t* _metric(lv_obj_t* parent, const char* label, lv_color_t color) {
    lv_obj_t* card = pm_ui_card(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* val = lv_label_create(card);
    lv_label_set_text(val, "0");
    lv_obj_set_style_text_color(val, color, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);

    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, label);
    lv_obj_set_style_text_color(lab, PM_C_FG_DIM, 0);
    return val;
}

static void _section(lv_obj_t* parent, const char* title) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, PM_C_ACCENT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
}

static void _build_screen(void) {
    if (s_screen) return;
    s_screen = pm_ui_screen();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x071019), 0);

    lv_obj_t* bar = pm_ui_titlebar(s_screen, "THE CLINICIAN", _back_cb, NULL);
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);
    s_lbl_source = lv_label_create(bar);
    lv_label_set_text(s_lbl_source, "no dataset loaded");
    lv_obj_set_style_text_color(s_lbl_source, PM_C_ACCENT, 0);

    lv_obj_t* top = lv_obj_create(s_screen);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 84);
    lv_obj_set_style_pad_all(top, 8, 0);
    lv_obj_set_style_pad_gap(top, 8, 0);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    s_lbl_records = _metric(top, "RECORDS", PM_C_ACCENT);
    s_lbl_risk = _metric(top, "RISK", PM_C_WARN);
    s_lbl_open = _metric(top, "OPEN/WEP", PM_C_ERR);
    s_lbl_gps = _metric(top, "GPS TAGGED", PM_C_OK);
    s_lbl_avg = _metric(top, "AVG RSSI", lv_color_hex(0xffd166));
    s_lbl_channel = _metric(top, "BUSY CH", lv_color_hex(0xc89eff));

    lv_obj_t* body = lv_obj_create(s_screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, 8, 0);
    lv_obj_set_style_pad_bottom(body, 8, 0);
    lv_obj_set_style_pad_gap(body, 8, 0);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    lv_obj_t* left = pm_ui_card(body);
    lv_obj_set_size(left, 250, LV_PCT(100));
    _section(left, "DATA INTAKE");
    pm_ui_button(left, "CAPTURE C6 SNAPSHOT", _capture_cb, NULL);
    pm_ui_button(left, "LOAD LATEST CSV", _load_cb, NULL);
    pm_ui_button(left, "WRITE REPORT", _report_cb, NULL);
    pm_ui_button(left, "CLEAR", _clear_cb, NULL);
    s_lbl_status = lv_label_create(left);
    lv_label_set_text(s_lbl_status, "ready");
    lv_obj_set_width(s_lbl_status, LV_PCT(100));
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_lbl_status, PM_C_FG_DIM, 0);

    _section(left, "NATIVE LIMITS");
    lv_obj_t* limits = lv_label_create(left);
    lv_label_set_text(limits,
        "Full XLSX workbench, Leaflet heatmaps, and browser File API remain WebOS features. "
        "This field app handles direct capture, CSV loading, and RF risk triage.");
    lv_obj_set_width(limits, LV_PCT(100));
    lv_label_set_long_mode(limits, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(limits, PM_C_FG_DIM, 0);

    lv_obj_t* center = pm_ui_card(body);
    lv_obj_set_flex_grow(center, 1);
    _section(center, "CLINICAL RF ANALYSIS");
    s_report = lv_label_create(center);
    lv_obj_set_width(s_report, LV_PCT(100));
    lv_label_set_long_mode(s_report, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_report, PM_C_FG, 0);
    lv_obj_set_flex_grow(s_report, 1);

    _section(center, "CHANNEL PRESSURE");
    lv_obj_t* channels = lv_obj_create(center);
    lv_obj_remove_style_all(channels);
    lv_obj_set_width(channels, LV_PCT(100));
    lv_obj_set_height(channels, 92);
    lv_obj_set_layout(channels, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(channels, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(channels, 5, 0);
    for (int i = 0; i < 14; i++) {
        s_channel_bars[i] = lv_bar_create(channels);
        lv_bar_set_range(s_channel_bars[i], 0, 100);
        lv_bar_set_value(s_channel_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_flex_grow(s_channel_bars[i], 1);
        lv_obj_set_height(s_channel_bars[i], 78);
        lv_obj_set_style_bg_color(s_channel_bars[i], lv_color_hex(0x102436), 0);
        lv_obj_set_style_bg_color(s_channel_bars[i], PM_C_ACCENT, LV_PART_INDICATOR);
    }

    lv_obj_t* right = pm_ui_card(body);
    lv_obj_set_size(right, 335, LV_PCT(100));
    _section(right, "TOP SIGNALS");
    s_table = lv_obj_create(right);
    lv_obj_remove_style_all(s_table);
    lv_obj_set_width(s_table, LV_PCT(100));
    lv_obj_set_flex_grow(s_table, 1);
    lv_obj_set_layout(s_table, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_table, 3, 0);
}

static int _cmp_record_risk(const void* a, const void* b) {
    const cl_record_t* ra = (const cl_record_t*)a;
    const cl_record_t* rb = (const cl_record_t*)b;
    int risk_a = (strcmp(ra->enc, "OPEN") == 0 ? 100 : 0) + (ra->rssi + 100);
    int risk_b = (strcmp(rb->enc, "OPEN") == 0 ? 100 : 0) + (rb->rssi + 100);
    return risk_b - risk_a;
}

static int _copy_for_render(cl_stats_t* stats, int channel_hits[14]) {
    if (!_ensure_state()) return 0;
    int n = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        *stats = s_stats;
        memcpy(channel_hits, s_channel_hits, sizeof(s_channel_hits));
        n = s_record_count;
        if (n > CL_MAX_RECORDS) n = CL_MAX_RECORDS;
        memcpy(s_render_records, s_records, n * sizeof(s_render_records[0]));
        xSemaphoreGive(s_lock);
    }
    return n;
}

static void _render_table(int count) {
    if (!s_table) return;
    lv_obj_clean(s_table);
    int rows = count < CL_VISIBLE_ROWS ? count : CL_VISIBLE_ROWS;
    for (int i = 0; i < rows; i++) {
        cl_record_t* r = &s_render_records[i];
        lv_obj_t* row = lv_obj_create(s_table);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 38);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_pad_ver(row, 2, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(i % 2 ? 0x0b1b2a : 0x102436), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

        char line[80];
        snprintf(line, sizeof(line), "%-22.22s %s", r->ssid[0] ? r->ssid : "(hidden)", r->enc);
        lv_obj_t* a = lv_label_create(row);
        lv_label_set_text(a, line);
        lv_obj_set_style_text_color(a,
            strcmp(r->enc, "OPEN") == 0 ? PM_C_ERR : PM_C_FG, 0);

        snprintf(line, sizeof(line), "%s  CH%d  %ddBm  hits %d",
                 r->bssid, r->channel, r->rssi, r->hits);
        lv_obj_t* b = lv_label_create(row);
        lv_label_set_text(b, line);
        lv_obj_set_style_text_color(b, PM_C_FG_DIM, 0);
    }
}

static void _render(void) {
    if (!s_screen) return;
    cl_stats_t stats;
    int channel_hits[14] = {0};
    int count = _copy_for_render(&stats, channel_hits);
    qsort(s_render_records, count, sizeof(s_render_records[0]), _cmp_record_risk);

    char buf[128];
    if (s_lbl_source) lv_label_set_text(s_lbl_source, s_source_label);
    if (s_lbl_status) lv_label_set_text(s_lbl_status, s_status_line);
    if (s_lbl_records) { snprintf(buf, sizeof(buf), "%d", stats.records); lv_label_set_text(s_lbl_records, buf); }
    if (s_lbl_risk) {
        lv_label_set_text(s_lbl_risk, stats.risk_label);
        lv_obj_set_style_text_color(s_lbl_risk,
            strcmp(stats.risk_label, "HIGH") == 0 ? PM_C_ERR :
            strcmp(stats.risk_label, "MED") == 0 ? PM_C_WARN : PM_C_OK, 0);
    }
    if (s_lbl_open) { snprintf(buf, sizeof(buf), "%d", stats.open + stats.wep); lv_label_set_text(s_lbl_open, buf); }
    if (s_lbl_gps) { snprintf(buf, sizeof(buf), "%d", stats.gps); lv_label_set_text(s_lbl_gps, buf); }
    if (s_lbl_avg) { snprintf(buf, sizeof(buf), "%d", stats.avg_rssi); lv_label_set_text(s_lbl_avg, buf); }
    if (s_lbl_channel) { snprintf(buf, sizeof(buf), "%d", stats.busiest_channel); lv_label_set_text(s_lbl_channel, buf); }

    int max_ch = 1;
    for (int i = 0; i < 14; i++) if (channel_hits[i] > max_ch) max_ch = channel_hits[i];
    for (int i = 0; i < 14; i++) {
        int pct = (channel_hits[i] * 100) / max_ch;
        if (s_channel_bars[i]) {
            lv_bar_set_value(s_channel_bars[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_channel_bars[i],
                                      pct > 70 ? PM_C_ERR :
                                      pct > 35 ? PM_C_WARN : PM_C_ACCENT,
                                      LV_PART_INDICATOR);
        }
    }

    if (s_report) {
        char report[768];
        snprintf(report, sizeof(report),
            "Risk score: %d (%s)\n\n"
            "Open networks: %d\n"
            "WEP legacy networks: %d\n"
            "Hidden SSIDs: %d\n"
            "Very strong signals: %d\n"
            "GPS coverage: %d / %d records\n"
            "Busiest channel: %d with %d observations\n\n"
            "Interpretation:\n"
            "%s%s%s"
            "Native mode is suitable for field triage. Use the WebOS Clinician for XLSX import, "
            "pivot tables, joins, heatmaps, and formal spreadsheet work.",
            stats.risk_score, stats.risk_label,
            stats.open, stats.wep, stats.hidden, stats.strong,
            stats.gps, stats.records,
            stats.busiest_channel, stats.busiest_hits,
            stats.open ? "- Open networks require immediate review.\n" : "",
            stats.wep ? "- WEP indicates legacy cryptography exposure.\n" : "",
            stats.strong ? "- Strong nearby signals may deserve physical source verification.\n" : "");
        lv_label_set_text(s_report, report);
    }

    _render_table(count);
}

static void _enter(void) {
    _ensure_state();
    _build_screen();
    lv_screen_load(s_screen);
    s_dirty = true;
    _render();
    pm_log_i(TAG, "enter");
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    static uint32_t last = 0;
    uint32_t now = pm_millis();
    if (!s_dirty && now - last < 1000) return;
    if (now - last < 250) return;
    last = now;
    s_dirty = false;
    _render();
}

static void _exit_(void) {
    s_capture_pending = false;
    if (pm_wifi_scan_is_owner(CL_WIFI_SCAN_OWNER)) {
        esp_wifi_scan_stop();
        pm_wifi_scan_give(CL_WIFI_SCAN_OWNER);
    }
    pm_log_i(TAG, "exit");
}

static const pm_app_t _APP = {
    .id           = "clinician",
    .display_name = "CLINICIAN",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = NULL,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_clinician(void) { return &_APP; }
