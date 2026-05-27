// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_silas_creek.c - 7" native RF operations dashboard
//
//  This is not the browser Silas Creek Parkway page. It is the
//  P4/C6-native control surface that uses the same event shape:
//  wifi_seen, gps, bridge_status/source status. Browser-only
//  pieces live in WebOS; this app is for field testing on LVGL.
// ============================================================

#include "pm_app_silas_creek.h"

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

static const char* TAG = "PM_SILAS";

#define SILAS_MAX_RECORDS       192
#define SILAS_MAX_SCAN_RECORDS   96
#define SILAS_MAX_LOG_LINES      14
#define SILAS_VISIBLE_ROWS       11
#define SILAS_SCAN_DELAY_MS    1200
#define SILAS_EXPORTS_DIR      "/sd/exports"

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
    uint32_t first_ms;
    uint32_t last_ms;
} silas_record_t;

static silas_record_t* s_records = NULL;
static silas_record_t* s_render_records = NULL;
static int s_record_count = 0;
static int s_wifi_events = 0;
static int s_scan_rounds = 0;
static int s_open_count = 0;
static int s_gps_tagged = 0;
static int s_channel_hits[14];
static char s_log[SILAS_MAX_LOG_LINES][96];
static int s_log_count = 0;

static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_scan_worker = NULL;
static bool s_wifi_evt_registered = false;
static volatile bool s_running = false;
static volatile bool s_dirty = true;
static volatile bool s_scan_error_dirty = false;
static char s_scan_error[48] = "";

static lv_obj_t* s_screen = NULL;
static lv_obj_t* s_lbl_total = NULL;
static lv_obj_t* s_lbl_unique = NULL;
static lv_obj_t* s_lbl_open = NULL;
static lv_obj_t* s_lbl_gps = NULL;
static lv_obj_t* s_lbl_source = NULL;
static lv_obj_t* s_lbl_state = NULL;
static lv_obj_t* s_lbl_gps_state = NULL;
static lv_obj_t* s_net_list = NULL;
static lv_obj_t* s_log_list = NULL;
static lv_obj_t* s_btn_start = NULL;
static lv_obj_t* s_btn_stop = NULL;
static lv_obj_t* s_channel_bars[14];

static bool _ensure_state(void) {
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return false;
    }
    if (!s_records) {
        s_records = (silas_record_t*)pm_psram_calloc(SILAS_MAX_RECORDS,
                                                     sizeof(silas_record_t));
        if (!s_records) {
            s_records = (silas_record_t*)calloc(SILAS_MAX_RECORDS,
                                                sizeof(silas_record_t));
        }
        if (!s_records) return false;
    }
    if (!s_render_records) {
        s_render_records = (silas_record_t*)pm_psram_calloc(SILAS_MAX_RECORDS,
                                                            sizeof(silas_record_t));
        if (!s_render_records) {
            s_render_records = (silas_record_t*)calloc(SILAS_MAX_RECORDS,
                                                       sizeof(silas_record_t));
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

static void _append_log_locked(const char* line) {
    if (!line) return;
    if (s_log_count >= SILAS_MAX_LOG_LINES) {
        memmove(&s_log[0], &s_log[1],
                (SILAS_MAX_LOG_LINES - 1) * sizeof(s_log[0]));
        s_log_count = SILAS_MAX_LOG_LINES - 1;
    }
    _copy_text(s_log[s_log_count++], sizeof(s_log[0]), line);
}

static int _find_or_replace_locked(const char* bssid) {
    for (int i = 0; i < s_record_count; i++) {
        if (strcmp(s_records[i].bssid, bssid) == 0) return i;
    }
    if (s_record_count < SILAS_MAX_RECORDS) return s_record_count++;

    int oldest = 0;
    for (int i = 1; i < s_record_count; i++) {
        if (s_records[i].last_ms < s_records[oldest].last_ms) oldest = i;
    }
    memset(&s_records[oldest], 0, sizeof(s_records[oldest]));
    return oldest;
}

static void _recalc_locked(void) {
    s_open_count = 0;
    s_gps_tagged = 0;
    memset(s_channel_hits, 0, sizeof(s_channel_hits));
    for (int i = 0; i < s_record_count; i++) {
        if (strcmp(s_records[i].enc, "OPEN") == 0) s_open_count++;
        if (s_records[i].gps_valid) s_gps_tagged++;
        if (s_records[i].channel >= 1 && s_records[i].channel <= 14) {
            s_channel_hits[s_records[i].channel - 1] += s_records[i].hits;
        }
    }
}

static void _record_ap_locked(const wifi_ap_record_t* ap) {
    char bssid[18];
    _format_bssid(ap->bssid, bssid, sizeof(bssid));

    int idx = _find_or_replace_locked(bssid);
    silas_record_t* r = &s_records[idx];
    bool fresh = (r->hits == 0);

    _copy_text(r->bssid, sizeof(r->bssid), bssid);
    _copy_text(r->ssid, sizeof(r->ssid), (const char*)ap->ssid);
    _copy_text(r->enc, sizeof(r->enc), _auth_to_str(ap->authmode));
    r->rssi = ap->rssi;
    r->channel = ap->primary;
    r->last_ms = pm_millis();
    if (fresh) r->first_ms = r->last_ms;
    r->hits++;

    pm_gps_t g;
    pm_gps_state_get(&g);
    if (g.valid) {
        r->gps_valid = true;
        r->lat = g.lat;
        r->lng = g.lng;
    }
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

static void _set_scan_error(const char* msg) {
    snprintf(s_scan_error, sizeof(s_scan_error), "%s", msg ? msg : "scan error");
    s_scan_error_dirty = true;
    s_dirty = true;
}

static void _process_scan_done(void) {
    uint16_t found = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) {
        _set_scan_error(esp_err_to_name(err));
        return;
    }

    uint16_t wanted = found;
    if (wanted > SILAS_MAX_SCAN_RECORDS) wanted = SILAS_MAX_SCAN_RECORDS;

    wifi_ap_record_t* aps = NULL;
    if (wanted > 0) {
        aps = (wifi_ap_record_t*)calloc(wanted, sizeof(wifi_ap_record_t));
        if (!aps) {
            esp_wifi_clear_ap_list();
            _set_scan_error("scan alloc failed");
            return;
        }
        uint16_t got = wanted;
        err = esp_wifi_scan_get_ap_records(&got, aps);
        if (err != ESP_OK) {
            free(aps);
            esp_wifi_clear_ap_list();
            _set_scan_error(esp_err_to_name(err));
            return;
        }
        wanted = got;
    }
    esp_wifi_clear_ap_list();

    if (_ensure_state() && xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_scan_rounds++;
        s_wifi_events += wanted;
        for (uint16_t i = 0; i < wanted; i++) {
            _record_ap_locked(&aps[i]);
        }
        _recalc_locked();

        char line[96];
        snprintf(line, sizeof(line), "%02u:%02u  wifi_scan  %u APs  total %d",
                 (unsigned)((pm_uptime_seconds() / 60) % 60),
                 (unsigned)(pm_uptime_seconds() % 60),
                 (unsigned)found, s_record_count);
        _append_log_locked(line);

        if (found > wanted) {
            snprintf(line, sizeof(line), "scan capped at %u of %u AP records",
                     (unsigned)wanted, (unsigned)found);
            _append_log_locked(line);
        }

        xSemaphoreGive(s_lock);
    }

    free(aps);
    s_dirty = true;
}

static void _scan_worker_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "scan worker started");
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_running) continue;

        _process_scan_done();

        if (s_running) {
            pm_delay_ms(SILAS_SCAN_DELAY_MS);
            esp_err_t err = _start_wifi_scan();
            if (err != ESP_OK) {
                s_running = false;
                _set_scan_error(esp_err_to_name(err));
            }
        }
    }
}

static void _wifi_event_handler(void* arg, esp_event_base_t base,
                                int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;
    if (!s_running || !s_scan_worker) return;
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
                                            "silas_scan",
                                            12288,
                                            NULL,
                                            4,
                                            &s_scan_worker,
                                            0);
    if (ok != pdPASS) {
        s_scan_worker = NULL;
        _set_scan_error("worker create failed");
    }
}

static void _start_scan(void) {
    if (s_running) return;
    if (!_ensure_state()) {
        _set_scan_error("state allocation failed");
        return;
    }

    _ensure_worker();
    if (!s_scan_worker) return;
    _register_wifi_events();

    s_running = true;
    esp_err_t err = _start_wifi_scan();
    if (err != ESP_OK) {
        s_running = false;
        _set_scan_error(esp_err_to_name(err));
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        _append_log_locked("scan started on C6 ESP-Hosted WiFi");
        xSemaphoreGive(s_lock);
    }
    s_dirty = true;
}

static void _stop_scan(void) {
    if (!s_running) return;
    s_running = false;
    esp_wifi_scan_stop();
    if (_ensure_state() && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        _append_log_locked("scan stopped");
        xSemaphoreGive(s_lock);
    }
    s_dirty = true;
}

static void _start_cb(lv_event_t* e) {
    (void)e;
    _start_scan();
}

static void _stop_cb(lv_event_t* e) {
    (void)e;
    _stop_scan();
}

static void _back_cb(lv_event_t* e) {
    (void)e;
    _stop_scan();
    pm_launcher_show();
}

static void _export_cb(lv_event_t* e) {
    (void)e;
    if (!pm_sd_mounted()) {
        _set_scan_error("SD not mounted");
        return;
    }
    if (!_ensure_state()) return;

    pm_file_mkdir(SILAS_EXPORTS_DIR);
    char path[96];
    snprintf(path, sizeof(path), "%s/silas_%010u.csv",
             SILAS_EXPORTS_DIR, (unsigned)pm_uptime_seconds());

    bool ok = false;
    PM_SPI_TAKE("silas_export") {
        pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            pm_file_printf(f, "bssid,ssid,enc,channel,rssi,hits,lat,lng,source\n");
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(300)) == pdTRUE) {
                for (int i = 0; i < s_record_count; i++) {
                    silas_record_t* r = &s_records[i];
                    pm_file_printf(f, "%s,%s,%s,%d,%d,%d,%.6f,%.6f,c6_hosted\n",
                                   r->bssid, r->ssid, r->enc, r->channel,
                                   r->rssi, r->hits,
                                   r->gps_valid ? r->lat : 0.0,
                                   r->gps_valid ? r->lng : 0.0);
                }
                xSemaphoreGive(s_lock);
                ok = true;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        char line[96];
        if (ok) {
            snprintf(line, sizeof(line), "exported silas CSV");
        } else {
            snprintf(line, sizeof(line), "export failed");
        }
        _append_log_locked(line);
        xSemaphoreGive(s_lock);
    }
    s_dirty = true;
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
    lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0);

    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, label);
    lv_obj_set_style_text_color(lab, PM_C_FG_DIM, 0);
    return val;
}

static void _make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, PM_C_ACCENT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
}

static void _build_screen(void) {
    if (s_screen) return;

    s_screen = pm_ui_screen();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x06111c), 0);

    lv_obj_t* bar = pm_ui_titlebar(s_screen, "SILAS CREEK", _back_cb, NULL);
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);
    s_lbl_source = lv_label_create(bar);
    lv_label_set_text(s_lbl_source, "C6 HOSTED  GPS --  SD --");
    lv_obj_set_style_text_color(s_lbl_source, PM_C_ACCENT, 0);

    lv_obj_t* stats = lv_obj_create(s_screen);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), 84);
    lv_obj_set_style_pad_all(stats, 8, 0);
    lv_obj_set_style_pad_gap(stats, 8, 0);
    lv_obj_set_layout(stats, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    s_lbl_total = _metric(stats, "OBS", lv_color_hex(0x4dd9ff));
    s_lbl_unique = _metric(stats, "UNIQUE AP", lv_color_hex(0x4dffa6));
    s_lbl_open = _metric(stats, "OPEN", lv_color_hex(0xff5577));
    s_lbl_gps = _metric(stats, "GPS TAGS", lv_color_hex(0xffd166));

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
    lv_obj_set_size(left, 330, LV_PCT(100));
    _make_section_label(left, "NETWORKS");
    s_net_list = lv_obj_create(left);
    lv_obj_remove_style_all(s_net_list);
    lv_obj_set_width(s_net_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_net_list, 1);
    lv_obj_set_style_bg_opa(s_net_list, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_net_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_net_list, 2, 0);

    lv_obj_t* center = pm_ui_card(body);
    lv_obj_set_flex_grow(center, 1);
    _make_section_label(center, "CHANNEL PRESSURE");
    lv_obj_t* channels = lv_obj_create(center);
    lv_obj_remove_style_all(channels);
    lv_obj_set_width(channels, LV_PCT(100));
    lv_obj_set_height(channels, 185);
    lv_obj_set_layout(channels, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(channels, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(channels, 5, 0);
    for (int i = 0; i < 14; i++) {
        lv_obj_t* col = lv_obj_create(channels);
        lv_obj_remove_style_all(col);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_layout(col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        s_channel_bars[i] = lv_bar_create(col);
        lv_bar_set_range(s_channel_bars[i], 0, 100);
        lv_bar_set_value(s_channel_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_size(s_channel_bars[i], 14, 130);
        lv_obj_set_style_bg_color(s_channel_bars[i], lv_color_hex(0x102436), 0);
        lv_obj_set_style_bg_color(s_channel_bars[i], lv_color_hex(0x4dd9ff),
                                  LV_PART_INDICATOR);

        lv_obj_t* lab = lv_label_create(col);
        char cbuf[4];
        snprintf(cbuf, sizeof(cbuf), "%d", i + 1);
        lv_label_set_text(lab, cbuf);
        lv_obj_set_style_text_color(lab, PM_C_FG_DIM, 0);
    }

    s_lbl_state = lv_label_create(center);
    lv_label_set_text(s_lbl_state, "READY - direct P4/C6 scan path");
    lv_obj_set_style_text_color(s_lbl_state, PM_C_FG, 0);
    lv_obj_set_style_text_font(s_lbl_state, &lv_font_montserrat_14, 0);

    s_lbl_gps_state = lv_label_create(center);
    lv_label_set_text(s_lbl_gps_state, "GPS waiting");
    lv_obj_set_style_text_color(s_lbl_gps_state, PM_C_FG_DIM, 0);

    lv_obj_t* right = pm_ui_card(body);
    lv_obj_set_size(right, 305, LV_PCT(100));
    _make_section_label(right, "LIVE OPERATIONS");
    s_log_list = lv_obj_create(right);
    lv_obj_remove_style_all(s_log_list);
    lv_obj_set_width(s_log_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_log_list, 1);
    lv_obj_set_style_bg_opa(s_log_list, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_log_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_log_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_log_list, 3, 0);

    lv_obj_t* actions = lv_obj_create(s_screen);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), 58);
    lv_obj_set_style_pad_all(actions, 8, 0);
    lv_obj_set_style_pad_gap(actions, 10, 0);
    lv_obj_set_style_bg_color(actions, lv_color_hex(0x081522), 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_COVER, 0);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_btn_start = pm_ui_button(actions, "START", _start_cb, NULL);
    s_btn_stop = pm_ui_button(actions, "STOP", _stop_cb, NULL);
    pm_ui_button(actions, "EXPORT CSV", _export_cb, NULL);
}

static int _copy_records_for_render(silas_record_t* out, int max_records,
                                    int* total, int* unique, int* open,
                                    int* gps, int channel_hits[14],
                                    char logs[SILAS_MAX_LOG_LINES][96],
                                    int* log_count) {
    if (!_ensure_state()) return 0;
    int n = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        *total = s_wifi_events;
        *unique = s_record_count;
        *open = s_open_count;
        *gps = s_gps_tagged;
        memcpy(channel_hits, s_channel_hits, sizeof(s_channel_hits));
        *log_count = s_log_count;
        memcpy(logs, s_log, sizeof(s_log));

        n = s_record_count;
        if (n > max_records) n = max_records;
        memcpy(out, s_records, n * sizeof(out[0]));
        xSemaphoreGive(s_lock);
    }
    return n;
}

static int _cmp_record_rssi(const void* a, const void* b) {
    const silas_record_t* ra = (const silas_record_t*)a;
    const silas_record_t* rb = (const silas_record_t*)b;
    return rb->rssi - ra->rssi;
}

static void _render_networks(const silas_record_t* records, int count) {
    if (!s_net_list) return;
    lv_obj_clean(s_net_list);
    int rows = count < SILAS_VISIBLE_ROWS ? count : SILAS_VISIBLE_ROWS;
    for (int i = 0; i < rows; i++) {
        const silas_record_t* r = &records[i];
        lv_obj_t* row = lv_obj_create(s_net_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 38);
        lv_obj_set_style_bg_color(row, lv_color_hex(i % 2 ? 0x0b1b2a : 0x102436), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_pad_ver(row, 2, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

        char line[64];
        snprintf(line, sizeof(line), "%-24.24s %ddBm", r->ssid[0] ? r->ssid : "(hidden)", r->rssi);
        lv_obj_t* l1 = lv_label_create(row);
        lv_label_set_text(l1, line);
        lv_obj_set_style_text_color(l1, r->rssi > -60 ? PM_C_OK : PM_C_FG, 0);

        char meta[80];
        snprintf(meta, sizeof(meta), "%s  CH%d  %s  hits %d",
                 r->bssid, r->channel, r->enc, r->hits);
        lv_obj_t* l2 = lv_label_create(row);
        lv_label_set_text(l2, meta);
        lv_obj_set_style_text_color(l2, PM_C_FG_DIM, 0);
    }
}

static void _render_logs(char logs[SILAS_MAX_LOG_LINES][96], int log_count) {
    if (!s_log_list) return;
    lv_obj_clean(s_log_list);
    for (int i = 0; i < log_count; i++) {
        lv_obj_t* l = lv_label_create(s_log_list);
        lv_label_set_text(l, logs[i]);
        lv_obj_set_style_text_color(l, i == log_count - 1 ? PM_C_ACCENT : PM_C_FG_DIM, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(100));
    }
}

static void _render(void) {
    if (!s_screen) return;

    int total = 0, unique = 0, open = 0, gps = 0;
    int channel_hits[14] = {0};
    char logs[SILAS_MAX_LOG_LINES][96];
    int log_count = 0;
    if (!_ensure_state()) return;
    int count = _copy_records_for_render(s_render_records, SILAS_MAX_RECORDS,
                                         &total, &unique, &open, &gps,
                                         channel_hits, logs, &log_count);
    qsort(s_render_records, count, sizeof(s_render_records[0]), _cmp_record_rssi);

    char buf[96];
    if (s_lbl_total)  { snprintf(buf, sizeof(buf), "%d", total);  lv_label_set_text(s_lbl_total, buf); }
    if (s_lbl_unique) { snprintf(buf, sizeof(buf), "%d", unique); lv_label_set_text(s_lbl_unique, buf); }
    if (s_lbl_open)   { snprintf(buf, sizeof(buf), "%d", open);   lv_label_set_text(s_lbl_open, buf); }
    if (s_lbl_gps)    { snprintf(buf, sizeof(buf), "%d", gps);    lv_label_set_text(s_lbl_gps, buf); }

    pm_gps_t g;
    pm_gps_state_get(&g);
    if (s_lbl_source) {
        snprintf(buf, sizeof(buf), "C6 HOSTED  GPS %s%d  SD %s",
                 g.valid ? "" : (g.sats > 0 ? "?" : "-- "),
                 g.valid || g.sats > 0 ? (int)g.sats : 0,
                 pm_sd_mounted() ? "OK" : "--");
        lv_label_set_text(s_lbl_source, buf);
    }

    if (s_lbl_state) {
        if (s_scan_error_dirty) {
            lv_label_set_text(s_lbl_state, s_scan_error);
            lv_obj_set_style_text_color(s_lbl_state, PM_C_ERR, 0);
            s_scan_error_dirty = false;
        } else {
            snprintf(buf, sizeof(buf), "%s - rounds %d - %d unique",
                     s_running ? "SCANNING" : "READY", s_scan_rounds, unique);
            lv_label_set_text(s_lbl_state, buf);
            lv_obj_set_style_text_color(s_lbl_state,
                                        s_running ? PM_C_OK : PM_C_FG, 0);
        }
    }

    if (s_lbl_gps_state) {
        if (g.valid) {
            snprintf(buf, sizeof(buf), "GPS FIX %.6f %.6f sats %d",
                     g.lat, g.lng, (int)g.sats);
        } else if (g.sats > 0) {
            snprintf(buf, sizeof(buf), "GPS visible but no fix: sats %d", (int)g.sats);
        } else {
            snprintf(buf, sizeof(buf), "GPS waiting on Cardputer UART1 header");
        }
        lv_label_set_text(s_lbl_gps_state, buf);
    }

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

    _render_networks(s_render_records, count);
    _render_logs(logs, log_count);

    if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, s_running ? 60 : LV_OPA_COVER, 0);
    if (s_btn_stop) lv_obj_set_style_bg_opa(s_btn_stop, s_running ? LV_OPA_COVER : 60, 0);
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
    _stop_scan();
    pm_log_i(TAG, "exit");
}

static const pm_app_t _APP = {
    .id           = "silas_creek",
    .display_name = "SILAS CREEK",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = NULL,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_silas_creek(void) { return &_APP; }
