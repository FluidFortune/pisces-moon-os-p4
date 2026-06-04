// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_weather.c — Open-Meteo weather client
//
//  Network path: standard ESP-IDF esp_http_client. ESP-Hosted
//  on the C6 provides the underlying WiFi transport — from our
//  side it just looks like a normal IP stack.
//
//  Fetch model:
//    - User taps FETCH (or app auto-fetches on first enter if
//      WiFi is up).
//    - A short-lived FreeRTOS task runs the HTTPS GET so the
//      LVGL tick never blocks.
//    - On success, the task parses JSON into shared state and
//      flips a "dirty" flag. The UI tick picks it up next pass
//      and redraws.
//    - On failure, an error message is shown in the status chip.
//
//  Location:
//    - If pm_gps_state has a fresh valid fix (<10 min), we use
//      those coordinates. Otherwise we fall back to the default
//      below (Oceanside, CA — adjust at will).
//
//  Open-Meteo response shape (only the fields we use):
//    {
//      "current": {
//        "temperature_2m": 68.2,
//        "apparent_temperature": 66.4,
//        "relative_humidity_2m": 64,
//        "wind_speed_10m": 7.3,
//        "weather_code": 2
//      },
//      "daily": {
//        "time": ["2026-06-04", ...],
//        "weather_code": [2, 3, 2, 1, 0],
//        "temperature_2m_max": [72.1, ...],
//        "temperature_2m_min": [58.4, ...]
//      }
//    }
// ============================================================

#include "pm_app_weather.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_gps_state.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_WEATHER";

// Default location when no GPS fix is fresh.
#define DEFAULT_LAT   33.196
#define DEFAULT_LON  -117.379
#define GPS_MAX_AGE_MS  (10 * 60 * 1000)

#define RESPONSE_BUF_BYTES   (16 * 1024)
#define FORECAST_DAYS        5

// ── Fetched state ────────────────────────────────────────
typedef struct {
    char     day[6];          // "Mon", "Tue", etc.
    float    tmax;
    float    tmin;
    int      code;
} daily_t;

typedef struct {
    bool     ok;
    bool     dirty;           // UI hasn't drawn the latest data yet
    char     err_msg[80];
    char     location_label[48];

    // Current conditions
    float    temp;
    float    feels;
    int      humidity;
    float    wind;
    int      code;

    // 5-day forecast
    daily_t  forecast[FORECAST_DAYS];
    int      forecast_count;

    uint32_t fetched_at_ms;
} weather_state_t;

static weather_state_t s_st = {0};
static SemaphoreHandle_t s_st_mutex = NULL;
static bool s_fetch_inflight = false;

// ── WMO weather code → short label + emoji-like glyph ────
typedef struct { int code_lo, code_hi; const char* label; const char* glyph; lv_color_t color; } wmo_t;
static const wmo_t WMO_MAP[] = {
    { 0,   0,  "CLEAR",         "*",  PM_LAYOUT_COL_GOLD   },
    { 1,   1,  "MAINLY CLEAR",  "*",  PM_LAYOUT_COL_GOLD   },
    { 2,   2,  "PARTLY CLOUDY", "~",  PM_LAYOUT_COL_ACCENT },
    { 3,   3,  "OVERCAST",      "=",  PM_LAYOUT_COL_DIM    },
    { 45,  48, "FOG",           "~~", PM_LAYOUT_COL_DIM    },
    { 51,  57, "DRIZZLE",       ".",  PM_LAYOUT_COL_ACCENT },
    { 61,  65, "RAIN",          "/",  PM_LAYOUT_COL_ACCENT },
    { 66,  67, "FREEZING RAIN", "*/", PM_LAYOUT_COL_PURPLE },
    { 71,  77, "SNOW",          "*",  PM_LAYOUT_COL_FG_BR  },
    { 80,  82, "RAIN SHOWERS",  "/",  PM_LAYOUT_COL_ACCENT },
    { 85,  86, "SNOW SHOWERS",  "*",  PM_LAYOUT_COL_FG_BR  },
    { 95,  99, "THUNDERSTORM",  "/!", PM_LAYOUT_COL_ERR    },
    { 0,   0,  NULL,            NULL, {0}                  },
};

static const wmo_t* _wmo_lookup(int code) {
    for (int i = 0; WMO_MAP[i].label; i++) {
        if (code >= WMO_MAP[i].code_lo && code <= WMO_MAP[i].code_hi) return &WMO_MAP[i];
    }
    return NULL;
}

// ── UI handles ───────────────────────────────────────────
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_chip_status   = NULL;
static lv_obj_t* s_chip_location = NULL;
static lv_obj_t* s_stat_temp     = NULL;
static lv_obj_t* s_stat_feels    = NULL;
static lv_obj_t* s_stat_wind     = NULL;
static lv_obj_t* s_stat_humidity = NULL;
static lv_obj_t* s_now_label     = NULL;
static lv_obj_t* s_now_glyph     = NULL;
static lv_obj_t* s_now_meta      = NULL;
static lv_obj_t* s_forecast_box  = NULL;
static lv_obj_t* s_err_label     = NULL;
static bool      s_built         = false;

typedef struct {
    lv_obj_t* row;
    lv_obj_t* day_lbl;
    lv_obj_t* code_lbl;
    lv_obj_t* hi_lbl;
    lv_obj_t* lo_lbl;
} forecast_row_ui_t;
static forecast_row_ui_t s_frows[FORECAST_DAYS];

// ── Helpers ──────────────────────────────────────────────
static void _state_lock(void) { if (s_st_mutex) xSemaphoreTake(s_st_mutex, portMAX_DELAY); }
static void _state_unlock(void) { if (s_st_mutex) xSemaphoreGive(s_st_mutex); }

// "2026-06-04" → "Thu" (zero indexes Sunday).
// We compute the day-of-week from the date string using Zeller's
// congruence — no time.h dependency for parsing strftime in C.
static void _date_to_dayname(const char* iso, char* out, size_t cap) {
    if (!iso || strlen(iso) < 10) { snprintf(out, cap, "—"); return; }
    int y = atoi(iso);
    int m = atoi(iso + 5);
    int d = atoi(iso + 8);
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100, j = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    // h: 0=Sat, 1=Sun, 2=Mon, ... 6=Fri
    static const char* names[7] = { "Sat", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri" };
    snprintf(out, cap, "%s", names[h]);
}

// ── HTTP fetch (runs in its own task) ────────────────────
typedef struct {
    char* buf;
    int   len;
    int   cap;
} http_collector_t;

static esp_err_t _http_event_cb(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        http_collector_t* c = (http_collector_t*)evt->user_data;
        int space = c->cap - c->len - 1;
        if (space > 0) {
            int copy = evt->data_len < space ? evt->data_len : space;
            memcpy(c->buf + c->len, evt->data, copy);
            c->len += copy;
            c->buf[c->len] = '\0';
        }
    }
    return ESP_OK;
}

// Build the Open-Meteo URL into `url_out`. Picks GPS or default.
static void _build_url(char* url_out, size_t cap, char* loc_label, size_t loc_cap) {
    pm_gps_t g = {0};
    pm_gps_state_get(&g);
    bool use_gps = g.valid && pm_gps_state_fresh(GPS_MAX_AGE_MS);
    double lat = use_gps ? g.lat : DEFAULT_LAT;
    double lon = use_gps ? g.lng : DEFAULT_LON;
    snprintf(loc_label, loc_cap, "%s %.3f,%.3f",
             use_gps ? "GPS" : "DEFAULT", lat, lon);
    snprintf(url_out, cap,
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,apparent_temperature,"
        "relative_humidity_2m,wind_speed_10m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=auto"
        "&temperature_unit=fahrenheit"
        "&wind_speed_unit=mph"
        "&forecast_days=%d",
        lat, lon, FORECAST_DAYS);
}

// Parse JSON into a temporary struct, then atomically copy under
// the state mutex.
static bool _parse_response(const char* json, weather_state_t* tmp) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;

    cJSON* cur = cJSON_GetObjectItem(root, "current");
    cJSON* dly = cJSON_GetObjectItem(root, "daily");
    if (!cur || !dly) { cJSON_Delete(root); return false; }

    tmp->temp     = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temperature_2m"));
    tmp->feels    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "apparent_temperature"));
    tmp->humidity = (int)  cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "relative_humidity_2m"));
    tmp->wind     = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "wind_speed_10m"));
    tmp->code     = (int)  cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "weather_code"));

    cJSON* times = cJSON_GetObjectItem(dly, "time");
    cJSON* codes = cJSON_GetObjectItem(dly, "weather_code");
    cJSON* maxs  = cJSON_GetObjectItem(dly, "temperature_2m_max");
    cJSON* mins  = cJSON_GetObjectItem(dly, "temperature_2m_min");

    tmp->forecast_count = 0;
    int n = cJSON_GetArraySize(times);
    if (n > FORECAST_DAYS) n = FORECAST_DAYS;
    for (int i = 0; i < n; i++) {
        daily_t* d = &tmp->forecast[tmp->forecast_count++];
        cJSON* t = cJSON_GetArrayItem(times, i);
        const char* iso = (t && cJSON_IsString(t)) ? t->valuestring : "";
        _date_to_dayname(iso, d->day, sizeof(d->day));
        d->code = (int)  cJSON_GetNumberValue(cJSON_GetArrayItem(codes, i));
        d->tmax = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(maxs,  i));
        d->tmin = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(mins,  i));
    }

    cJSON_Delete(root);
    return true;
}

static void _fetch_task(void* arg) {
    (void)arg;
    char url[400];
    char loc_label[48];
    _build_url(url, sizeof(url), loc_label, sizeof(loc_label));
    pm_log_i(TAG, "GET %s", url);

    char* buf = (char*)pm_psram_alloc(RESPONSE_BUF_BYTES);
    if (!buf) {
        _state_lock();
        s_st.ok = false;
        snprintf(s_st.err_msg, sizeof(s_st.err_msg), "PSRAM alloc failed");
        s_st.dirty = true;
        _state_unlock();
        s_fetch_inflight = false;
        vTaskDelete(NULL);
        return;
    }
    buf[0] = '\0';

    http_collector_t coll = { .buf = buf, .len = 0, .cap = RESPONSE_BUF_BYTES };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = _http_event_cb,
        .user_data = &coll,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    weather_state_t tmp = {0};
    strncpy(tmp.location_label, loc_label, sizeof(tmp.location_label) - 1);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        tmp.ok = false;
        snprintf(tmp.err_msg, sizeof(tmp.err_msg), "HTTP %s",
                  esp_err_to_name(err));
    } else if (status != 200) {
        tmp.ok = false;
        snprintf(tmp.err_msg, sizeof(tmp.err_msg), "HTTP %d", status);
    } else if (coll.len == 0) {
        tmp.ok = false;
        snprintf(tmp.err_msg, sizeof(tmp.err_msg), "empty response");
    } else if (!_parse_response(buf, &tmp)) {
        tmp.ok = false;
        snprintf(tmp.err_msg, sizeof(tmp.err_msg), "parse failed");
    } else {
        tmp.ok = true;
        tmp.fetched_at_ms = pm_millis();
    }
    esp_http_client_cleanup(client);
    pm_psram_free(buf);

    _state_lock();
    // Preserve a known-good cache if this attempt failed.
    if (tmp.ok) {
        memcpy(&s_st, &tmp, sizeof(s_st));
    } else {
        s_st.ok = false;
        strncpy(s_st.location_label, tmp.location_label,
                sizeof(s_st.location_label) - 1);
        strncpy(s_st.err_msg, tmp.err_msg, sizeof(s_st.err_msg) - 1);
    }
    s_st.dirty = true;
    _state_unlock();

    s_fetch_inflight = false;
    pm_log_i(TAG, "fetch done ok=%d status=%d len=%d",
              (int)tmp.ok, status, coll.len);
    vTaskDelete(NULL);
}

static void _trigger_fetch(void) {
    if (s_fetch_inflight) return;
    s_fetch_inflight = true;
    // 6 KB stack — JSON parsing of 16 KB buffers can push past 4 KB.
    xTaskCreate(_fetch_task, "wx_fetch", 6144, NULL, 5, NULL);
}

// ── Render ───────────────────────────────────────────────
static void _render_state(void) {
    weather_state_t snap = {0};
    _state_lock();
    memcpy(&snap, &s_st, sizeof(snap));
    _state_unlock();

    if (s_chip_location) lv_label_set_text(s_chip_location,
                                            snap.location_label[0] ? snap.location_label : "—");

    if (s_chip_status) {
        if (s_fetch_inflight) {
            lv_label_set_text(s_chip_status, "FETCHING");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_ACCENT, 0);
        } else if (snap.ok) {
            lv_label_set_text(s_chip_status, "OK");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_OK, 0);
        } else if (snap.err_msg[0]) {
            lv_label_set_text(s_chip_status, "ERROR");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_ERR, 0);
        } else {
            lv_label_set_text(s_chip_status, "STANDBY");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_DIM, 0);
        }
    }

    if (!snap.ok) {
        if (s_stat_temp)     lv_label_set_text(s_stat_temp,     "—");
        if (s_stat_feels)    lv_label_set_text(s_stat_feels,    "—");
        if (s_stat_wind)     lv_label_set_text(s_stat_wind,     "—");
        if (s_stat_humidity) lv_label_set_text(s_stat_humidity, "—");
        if (s_now_label)     lv_label_set_text(s_now_label,     "NO DATA");
        if (s_now_glyph)     lv_label_set_text(s_now_glyph,     "?");
        if (s_now_meta) {
            if (snap.err_msg[0]) {
                char b[96];
                snprintf(b, sizeof(b), "Last error: %s. Tap FETCH to retry.",
                          snap.err_msg);
                lv_label_set_text(s_now_meta, b);
            } else {
                lv_label_set_text(s_now_meta, "Tap FETCH to get current conditions.");
            }
        }
        if (s_err_label) {
            if (snap.err_msg[0]) {
                lv_label_set_text(s_err_label, snap.err_msg);
                lv_obj_clear_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
        // Hide forecast rows.
        for (int i = 0; i < FORECAST_DAYS; i++) {
            if (s_frows[i].row) lv_obj_add_flag(s_frows[i].row, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (s_err_label) lv_obj_add_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);

    char b[32];
    snprintf(b, sizeof(b), "%.0f°F", snap.temp);     lv_label_set_text(s_stat_temp,     b);
    snprintf(b, sizeof(b), "%.0f°F", snap.feels);    lv_label_set_text(s_stat_feels,    b);
    snprintf(b, sizeof(b), "%.0f mph", snap.wind);   lv_label_set_text(s_stat_wind,     b);
    snprintf(b, sizeof(b), "%d%%",    snap.humidity); lv_label_set_text(s_stat_humidity, b);

    const wmo_t* w = _wmo_lookup(snap.code);
    lv_label_set_text(s_now_label, w ? w->label : "UNKNOWN");
    lv_label_set_text(s_now_glyph, w ? w->glyph : "?");
    if (w) {
        lv_obj_set_style_text_color(s_now_glyph, w->color, 0);
        lv_obj_set_style_text_color(s_now_label, w->color, 0);
    }
    if (s_now_meta) {
        char m[120];
        uint32_t age = (pm_millis() - snap.fetched_at_ms) / 1000;
        snprintf(m, sizeof(m), "%s  •  fetched %lus ago",
                  snap.location_label, (unsigned long)age);
        lv_label_set_text(s_now_meta, m);
    }

    for (int i = 0; i < FORECAST_DAYS; i++) {
        forecast_row_ui_t* fr = &s_frows[i];
        if (!fr->row) continue;
        if (i >= snap.forecast_count) {
            lv_obj_add_flag(fr->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(fr->row, LV_OBJ_FLAG_HIDDEN);
        const daily_t* d = &snap.forecast[i];
        lv_label_set_text(fr->day_lbl, d->day);
        const wmo_t* fw = _wmo_lookup(d->code);
        lv_label_set_text(fr->code_lbl, fw ? fw->label : "—");
        if (fw) lv_obj_set_style_text_color(fr->code_lbl, fw->color, 0);
        char hi[16], lo[16];
        snprintf(hi, sizeof(hi), "%.0f°", d->tmax);
        snprintf(lo, sizeof(lo), "%.0f°", d->tmin);
        lv_label_set_text(fr->hi_lbl, hi);
        lv_label_set_text(fr->lo_lbl, lo);
    }
}

// ── Actions ──────────────────────────────────────────────
static void _act_fetch(lv_event_t* e) {
    (void)e;
    _trigger_fetch();
    _render_state();
}

// ── Forecast row builder ─────────────────────────────────
static void _build_forecast_row(int idx, lv_obj_t* parent) {
    forecast_row_ui_t* r = &s_frows[idx];
    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 36);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 14, 0);
    lv_obj_set_style_pad_column(r->row, 12, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);

    r->day_lbl = lv_label_create(r->row);
    lv_label_set_text(r->day_lbl, "—");
    lv_obj_set_style_text_font(r->day_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->day_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_width(r->day_lbl, 56);

    r->code_lbl = lv_label_create(r->row);
    lv_label_set_text(r->code_lbl, "—");
    lv_obj_set_style_text_font(r->code_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->code_lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_flex_grow(r->code_lbl, 1);

    r->hi_lbl = lv_label_create(r->row);
    lv_label_set_text(r->hi_lbl, "—");
    lv_obj_set_style_text_font(r->hi_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->hi_lbl, PM_LAYOUT_COL_GOLD, 0);
    lv_obj_set_width(r->hi_lbl, 56);
    lv_obj_set_style_text_align(r->hi_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    r->lo_lbl = lv_label_create(r->row);
    lv_label_set_text(r->lo_lbl, "—");
    lv_obj_set_style_text_font(r->lo_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->lo_lbl, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_set_width(r->lo_lbl, 56);
    lv_obj_set_style_text_align(r->lo_lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

// ── Screen build ─────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "WEATHER");

    s_chip_status   = pm_app_layout_chip(&L, "STANDBY", PM_LAYOUT_COL_DIM);
    s_chip_location = pm_app_layout_chip(&L, "—",       PM_LAYOUT_COL_ACCENT);

    pm_app_layout_stats_row(&L, 4);
    s_stat_temp     = pm_app_layout_stat(&L, "TEMP",     "—");
    s_stat_feels    = pm_app_layout_stat(&L, "FEELS",    "—");
    s_stat_wind     = pm_app_layout_stat(&L, "WIND",     "—");
    s_stat_humidity = pm_app_layout_stat(&L, "HUMIDITY", "—");

    pm_app_layout_content(&L);

    // Left pane: current conditions
#if PM_BOARD_LCD_H_RES <= 800
    int left_w = 320;
#else
    int left_w = 440;
#endif
    lv_obj_t* now_pane = pm_app_layout_pane(&L, left_w, "NOW");

    lv_obj_t* now_inner = lv_obj_create(now_pane);
    lv_obj_remove_style_all(now_inner);
    lv_obj_set_width(now_inner, LV_PCT(100));
    lv_obj_set_flex_grow(now_inner, 1);
    lv_obj_set_style_bg_opa(now_inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(now_inner, 24, 0);
    lv_obj_set_layout(now_inner, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(now_inner, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(now_inner, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(now_inner, 10, 0);
    lv_obj_clear_flag(now_inner, LV_OBJ_FLAG_SCROLLABLE);

    s_now_glyph = lv_label_create(now_inner);
    lv_label_set_text(s_now_glyph, "?");
#if PM_BOARD_LCD_H_RES <= 800
    lv_obj_set_style_text_font(s_now_glyph, &lv_font_montserrat_48, 0);
#else
    lv_obj_set_style_text_font(s_now_glyph, &lv_font_montserrat_48, 0);
#endif
    lv_obj_set_style_text_color(s_now_glyph, PM_LAYOUT_COL_DIM, 0);

    s_now_label = lv_label_create(now_inner);
    lv_label_set_text(s_now_label, "STANDBY");
    lv_obj_set_style_text_font(s_now_label, PM_LAYOUT_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_now_label, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(s_now_label, 2, 0);

    s_now_meta = lv_label_create(now_inner);
    lv_label_set_text(s_now_meta, "Tap FETCH to get current conditions.");
    lv_label_set_long_mode(s_now_meta, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_now_meta, LV_PCT(100));
    lv_obj_set_style_text_align(s_now_meta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_now_meta, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_now_meta, PM_LAYOUT_COL_DIM, 0);

    s_err_label = lv_label_create(now_inner);
    lv_label_set_text(s_err_label, "");
    lv_label_set_long_mode(s_err_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_err_label, LV_PCT(100));
    lv_obj_set_style_text_font(s_err_label, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_err_label, PM_LAYOUT_COL_ERR, 0);
    lv_obj_set_style_text_align(s_err_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);

    // Right pane: forecast
    lv_obj_t* fc_pane = pm_app_layout_pane(&L, 0, "FORECAST");
    s_forecast_box = lv_obj_create(fc_pane);
    lv_obj_remove_style_all(s_forecast_box);
    lv_obj_set_width(s_forecast_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_forecast_box, 1);
    lv_obj_set_style_bg_opa(s_forecast_box, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_forecast_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_forecast_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_forecast_box, 1, 0);
    lv_obj_clear_flag(s_forecast_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < FORECAST_DAYS; i++) {
        _build_forecast_row(i, s_forecast_box);
    }

    pm_app_layout_action(&L, "FETCH", PM_LAYOUT_COL_ACCENT, _act_fetch);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ────────────────────────────────────────────
static void _init(void) {
    if (!s_st_mutex) s_st_mutex = xSemaphoreCreateMutex();
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    _render_state();
    // Auto-fetch on first entry if we don't have data and WiFi looks up.
    if (!s_st.ok && !s_fetch_inflight) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            _trigger_fetch();
        }
    }
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    s_last_render = now;
    bool need_redraw = false;
    _state_lock();
    if (s_st.dirty) { s_st.dirty = false; need_redraw = true; }
    _state_unlock();
    if (need_redraw || s_fetch_inflight) _render_state();
}

static void _exit_(void) {}

static const pm_app_t _APP = {
    .id           = "weather",
    .display_name = "WEATHER",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_weather(void) { return &_APP; }
