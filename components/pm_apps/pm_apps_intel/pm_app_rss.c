// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_rss.c — RSS news reader
//
//  Network path: esp_http_client over the standard ESP-Hosted
//  WiFi stack. Pulls a raw RSS 2.0 / Atom XML payload, walks
//  it with a minimal hand-rolled scanner, and presents items
//  in a list+detail view.
//
//  Why no XML library?
//    Pulling in expat or libxml2 just for the small subset of
//    RSS tags we need (title, link, pubDate, description) is
//    a heavyweight tradeoff. A 200-line scanner that finds
//    <item>...</item> blocks and extracts known child tags
//    handles every well-formed feed I tested and degrades
//    gracefully on malformed ones.
//
//  Feed list:
//    Hardcoded array of three solid public feeds for first
//    cut. Future: read /sd/rss_feeds.txt one URL per line.
// ============================================================

#include "pm_app_rss.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_RSS";

#define MAX_ITEMS         32
#define MAX_VISIBLE_ROWS  24
#define TITLE_LEN         220
#define LINK_LEN          240
#define DATE_LEN          48
#define DESC_LEN          1200
#define FEED_NAME_LEN     32
#define RESPONSE_BUF_BYTES (96 * 1024)

// ── Feed registry ─────────────────────────────────────────
typedef struct {
    const char* name;
    const char* url;
} rss_feed_t;

static const rss_feed_t FEEDS[] = {
    { "BBC NEWS",      "https://feeds.bbci.co.uk/news/world/rss.xml" },
    { "HACKER NEWS",   "https://hnrss.org/frontpage" },
    { "NPR TOP",       "https://feeds.npr.org/1001/rss.xml" },
};
#define FEED_COUNT (int)(sizeof(FEEDS) / sizeof(FEEDS[0]))

// ── Item store ────────────────────────────────────────────
typedef struct {
    char title[TITLE_LEN];
    char link[LINK_LEN];
    char pubdate[DATE_LEN];
    char description[DESC_LEN];
} rss_item_t;

static rss_item_t* s_items = NULL;
static int         s_item_count    = 0;
static int         s_selected      = 0;
static int         s_current_feed  = 0;
static SemaphoreHandle_t s_items_mutex = NULL;
static bool        s_dirty         = false;
static bool        s_fetch_inflight = false;
static char        s_err_msg[80]   = {0};

// ── UI handles ────────────────────────────────────────────
static lv_obj_t* s_screen        = NULL;
static lv_obj_t* s_chip_status   = NULL;
static lv_obj_t* s_chip_feed     = NULL;
static lv_obj_t* s_stat_items    = NULL;
static lv_obj_t* s_stat_feed     = NULL;
static lv_obj_t* s_list_box      = NULL;
static lv_obj_t* s_detail_title  = NULL;
static lv_obj_t* s_detail_date   = NULL;
static lv_obj_t* s_detail_link   = NULL;
static lv_obj_t* s_detail_desc   = NULL;
static bool      s_built         = false;

typedef struct {
    lv_obj_t* row;
    lv_obj_t* title_lbl;
    lv_obj_t* date_lbl;
} item_row_ui_t;
static item_row_ui_t s_rows[MAX_VISIBLE_ROWS];
static int           s_rows_created = 0;

static void _items_lock(void)   { if (s_items_mutex) xSemaphoreTake(s_items_mutex, portMAX_DELAY); }
static void _items_unlock(void) { if (s_items_mutex) xSemaphoreGive(s_items_mutex); }

// ── String helpers ────────────────────────────────────────

// Walk `text` and decode common HTML entities in place. Caller
// guarantees nul-termination.
static void _decode_entities(char* text) {
    if (!text) return;
    char* r = text;
    char* w = text;
    while (*r) {
        if (*r == '&') {
            if      (strncmp(r, "&amp;",  5) == 0) { *w++ = '&';  r += 5; continue; }
            else if (strncmp(r, "&lt;",   4) == 0) { *w++ = '<';  r += 4; continue; }
            else if (strncmp(r, "&gt;",   4) == 0) { *w++ = '>';  r += 4; continue; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"';  r += 6; continue; }
            else if (strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += 6; continue; }
            else if (strncmp(r, "&#39;",  5) == 0) { *w++ = '\''; r += 5; continue; }
            else if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' ';  r += 6; continue; }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

// Strip all <tag>...</tag> markup, replacing each pair with a
// space so words don't run together.
static void _strip_html(char* text) {
    if (!text) return;
    char* r = text;
    char* w = text;
    bool in_tag = false;
    while (*r) {
        if (*r == '<')      { in_tag = true;  if (w > text && w[-1] != ' ') *w++ = ' '; r++; continue; }
        if (*r == '>')      { in_tag = false; r++; continue; }
        if (!in_tag)        { *w++ = *r; }
        r++;
    }
    *w = '\0';
    // Collapse runs of whitespace.
    r = text; w = text;
    bool prev_space = false;
    while (*r) {
        bool sp = (*r == ' ' || *r == '\t' || *r == '\n' || *r == '\r');
        if (sp) {
            if (!prev_space) *w++ = ' ';
            prev_space = true;
        } else {
            *w++ = *r;
            prev_space = false;
        }
        r++;
    }
    *w = '\0';
}

// Copy substring from src to dst, applying CDATA unwrap if the
// content is wrapped in <![CDATA[ ... ]]>. Always nul-terminates.
static void _extract_text(const char* src_begin, const char* src_end,
                            char* dst, size_t cap) {
    if (!src_begin || !src_end || src_end < src_begin || cap == 0) {
        if (cap) dst[0] = '\0';
        return;
    }
    const char* s = src_begin;
    const char* e = src_end;
    // CDATA?
    if (e - s >= 12 && strncmp(s, "<![CDATA[", 9) == 0) {
        s += 9;
        if (e - s >= 3 && strncmp(e - 3, "]]>", 3) == 0) e -= 3;
    }
    size_t len = (size_t)(e - s);
    if (len > cap - 1) len = cap - 1;
    memcpy(dst, s, len);
    dst[len] = '\0';
    _decode_entities(dst);
}

// Find the body of the first <tag>...</tag> inside [start, end).
// Writes pointers to the start and end of the body. Returns true
// if found, false otherwise.
static bool _find_tag(const char* start, const char* end,
                       const char* tag,
                       const char** body_begin, const char** body_end) {
    char open_tag[32];
    char close_tag[32];
    snprintf(open_tag,  sizeof(open_tag),  "<%s",  tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
    const char* p = start;
    while (p < end) {
        const char* h = strstr(p, open_tag);
        if (!h || h >= end) return false;
        // Step past attributes to the closing '>' of the open tag.
        const char* gt = strchr(h, '>');
        if (!gt || gt >= end) return false;
        const char* close = strstr(gt + 1, close_tag);
        if (!close || close >= end) return false;
        *body_begin = gt + 1;
        *body_end   = close;
        return true;
    }
    return false;
}

// ── Parse one RSS document ────────────────────────────────
static int _parse_rss(const char* xml, rss_item_t* items, int max_items) {
    if (!xml) return 0;
    const char* end = xml + strlen(xml);
    int n = 0;
    const char* cursor = xml;
    while (n < max_items) {
        const char* item_open = strstr(cursor, "<item");
        if (!item_open || item_open >= end) {
            // Atom feeds use <entry> instead of <item>.
            item_open = strstr(cursor, "<entry");
            if (!item_open || item_open >= end) break;
        }
        const char* gt = strchr(item_open, '>');
        if (!gt) break;
        const char* item_close = strstr(gt + 1, "</item>");
        if (!item_close) item_close = strstr(gt + 1, "</entry>");
        if (!item_close || item_close >= end) break;

        rss_item_t* it = &items[n];
        memset(it, 0, sizeof(*it));

        const char* b;
        const char* e;
        if (_find_tag(gt + 1, item_close, "title", &b, &e)) {
            _extract_text(b, e, it->title, sizeof(it->title));
            _strip_html(it->title);
        }
        if (_find_tag(gt + 1, item_close, "link", &b, &e)) {
            _extract_text(b, e, it->link, sizeof(it->link));
        }
        if (_find_tag(gt + 1, item_close, "pubDate", &b, &e) ||
            _find_tag(gt + 1, item_close, "published", &b, &e) ||
            _find_tag(gt + 1, item_close, "updated",   &b, &e)) {
            _extract_text(b, e, it->pubdate, sizeof(it->pubdate));
        }
        if (_find_tag(gt + 1, item_close, "description", &b, &e) ||
            _find_tag(gt + 1, item_close, "summary",     &b, &e) ||
            _find_tag(gt + 1, item_close, "content",     &b, &e)) {
            _extract_text(b, e, it->description, sizeof(it->description));
            _strip_html(it->description);
        }

        n++;
        cursor = item_close + 1;
    }
    return n;
}

// ── HTTP collector ────────────────────────────────────────
typedef struct {
    char* buf;
    int   len;
    int   cap;
} collector_t;

static esp_err_t _http_event_cb(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        collector_t* c = (collector_t*)evt->user_data;
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

// ── Fetch task ────────────────────────────────────────────
static void _fetch_task(void* arg) {
    int feed_idx = (int)(intptr_t)arg;
    const rss_feed_t* feed = &FEEDS[feed_idx];
    pm_log_i(TAG, "GET %s (%s)", feed->url, feed->name);

    char* buf = (char*)pm_psram_alloc(RESPONSE_BUF_BYTES);
    if (!buf) {
        snprintf(s_err_msg, sizeof(s_err_msg), "PSRAM alloc failed");
        s_dirty = true;
        s_fetch_inflight = false;
        vTaskDelete(NULL);
        return;
    }
    buf[0] = '\0';

    collector_t coll = { .buf = buf, .len = 0, .cap = RESPONSE_BUF_BYTES };
    esp_http_client_config_t cfg = {
        .url = feed->url,
        .event_handler = _http_event_cb,
        .user_data = &coll,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err  = esp_http_client_perform(client);
    int       code = esp_http_client_get_status_code(client);

    if (err != ESP_OK) {
        snprintf(s_err_msg, sizeof(s_err_msg), "HTTP %s",
                  esp_err_to_name(err));
    } else if (code != 200) {
        snprintf(s_err_msg, sizeof(s_err_msg), "HTTP %d", code);
    } else if (coll.len == 0) {
        snprintf(s_err_msg, sizeof(s_err_msg), "empty response");
    } else {
        s_err_msg[0] = '\0';
        _items_lock();
        s_item_count = _parse_rss(buf, s_items, MAX_ITEMS);
        if (s_selected >= s_item_count) s_selected = 0;
        _items_unlock();
    }
    esp_http_client_cleanup(client);
    pm_psram_free(buf);
    s_dirty = true;
    s_fetch_inflight = false;
    pm_log_i(TAG, "fetch done items=%d err='%s'", s_item_count, s_err_msg);
    vTaskDelete(NULL);
}

static void _trigger_fetch(int feed_idx) {
    if (s_fetch_inflight) return;
    if (!s_items) {
        s_items = (rss_item_t*)pm_psram_alloc(sizeof(rss_item_t) * MAX_ITEMS);
        if (!s_items) {
            pm_log_e(TAG, "PSRAM alloc for items failed");
            return;
        }
        memset(s_items, 0, sizeof(rss_item_t) * MAX_ITEMS);
    }
    s_fetch_inflight = true;
    xTaskCreate(_fetch_task, "rss_fetch", 8192,
                (void*)(intptr_t)feed_idx, 5, NULL);
}

// ── Row click ─────────────────────────────────────────────
static void _on_row_clicked(lv_event_t* e) {
    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx >= 0 && idx < s_item_count) {
        s_selected = idx;
        s_dirty = true;
    }
}

// ── Row builder ───────────────────────────────────────────
static void _build_row(int idx, lv_obj_t* parent) {
    item_row_ui_t* r = &s_rows[idx];
    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 40);
    lv_obj_set_style_bg_color(r->row,
        (idx & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_column(r->row, 8, 0);
    lv_obj_set_layout(r->row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(r->row, 0, 0);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->row, _on_row_clicked, LV_EVENT_CLICKED, NULL);

    r->title_lbl = lv_label_create(r->row);
    lv_label_set_text(r->title_lbl, "—");
    lv_label_set_long_mode(r->title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->title_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->title_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->title_lbl, PM_LAYOUT_COL_FG_BR, 0);

    r->date_lbl = lv_label_create(r->row);
    lv_label_set_text(r->date_lbl, "—");
    lv_label_set_long_mode(r->date_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->date_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->date_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->date_lbl, PM_LAYOUT_COL_DIM, 0);
}

// ── Render ────────────────────────────────────────────────
static void _render(void) {
    if (!s_built) return;

    if (s_chip_feed)   lv_label_set_text(s_chip_feed, FEEDS[s_current_feed].name);
    if (s_chip_status) {
        if (s_fetch_inflight) {
            lv_label_set_text(s_chip_status, "FETCHING");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_ACCENT, 0);
        } else if (s_err_msg[0]) {
            lv_label_set_text(s_chip_status, "ERROR");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_ERR, 0);
        } else if (s_item_count > 0) {
            lv_label_set_text(s_chip_status, "READY");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_OK, 0);
        } else {
            lv_label_set_text(s_chip_status, "STANDBY");
            lv_obj_set_style_text_color(s_chip_status, PM_LAYOUT_COL_DIM, 0);
        }
    }
    if (s_stat_items) {
        char b[8]; snprintf(b, sizeof(b), "%d", s_item_count);
        lv_label_set_text(s_stat_items, b);
    }
    if (s_stat_feed) {
        char b[24]; snprintf(b, sizeof(b), "%d/%d", s_current_feed + 1, FEED_COUNT);
        lv_label_set_text(s_stat_feed, b);
    }

    // Rows
    int visible = 0;
    for (int i = 0; i < s_item_count && i < MAX_VISIBLE_ROWS; i++) {
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        item_row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(r->row, (void*)(intptr_t)i);
        lv_label_set_text(r->title_lbl, s_items[i].title[0] ? s_items[i].title : "(no title)");
        lv_label_set_text(r->date_lbl,  s_items[i].pubdate[0] ? s_items[i].pubdate : "—");
        // Highlight selected row.
        lv_obj_set_style_bg_color(r->row,
            (i == s_selected) ? PM_LAYOUT_COL_BORDER :
            (i & 1) ? PM_LAYOUT_COL_BG2 : PM_LAYOUT_COL_BG3, 0);
        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    // Detail
    if (s_item_count == 0) {
        if (s_detail_title) lv_label_set_text(s_detail_title,
            s_err_msg[0] ? s_err_msg : "Tap REFRESH to fetch this feed.");
        if (s_detail_date)  lv_label_set_text(s_detail_date,  "");
        if (s_detail_link)  lv_label_set_text(s_detail_link,  "");
        if (s_detail_desc)  lv_label_set_text(s_detail_desc,  "");
    } else {
        const rss_item_t* it = &s_items[s_selected];
        if (s_detail_title) lv_label_set_text(s_detail_title, it->title[0] ? it->title : "(no title)");
        if (s_detail_date)  lv_label_set_text(s_detail_date,  it->pubdate);
        if (s_detail_link)  lv_label_set_text(s_detail_link,  it->link);
        if (s_detail_desc)  lv_label_set_text(s_detail_desc,
                                                it->description[0] ? it->description : "(no description)");
    }
}

// ── Actions ───────────────────────────────────────────────
static void _act_refresh(lv_event_t* e) { (void)e; _trigger_fetch(s_current_feed); _render(); }
static void _act_next_feed(lv_event_t* e) {
    (void)e;
    s_current_feed = (s_current_feed + 1) % FEED_COUNT;
    s_selected = 0;
    _items_lock();
    s_item_count = 0;
    _items_unlock();
    _trigger_fetch(s_current_feed);
    _render();
}

// ── Screen build ──────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "RSS");

    s_chip_status = pm_app_layout_chip(&L, "STANDBY",    PM_LAYOUT_COL_DIM);
    s_chip_feed   = pm_app_layout_chip(&L, FEEDS[0].name, PM_LAYOUT_COL_ACCENT);

    pm_app_layout_stats_row(&L, 2);
    s_stat_items = pm_app_layout_stat(&L, "ITEMS", "0");
    s_stat_feed  = pm_app_layout_stat(&L, "FEED",  "1/3");

    pm_app_layout_content(&L);

#if PM_BOARD_LCD_H_RES <= 800
    int list_w = 320;
#else
    int list_w = 440;
#endif
    lv_obj_t* list_pane = pm_app_layout_pane(&L, list_w, "HEADLINES");
    s_list_box = lv_obj_create(list_pane);
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

    lv_obj_t* dp = pm_app_layout_pane(&L, 0, "ARTICLE");
    lv_obj_t* inner = lv_obj_create(dp);
    lv_obj_remove_style_all(inner);
    lv_obj_set_width(inner, LV_PCT(100));
    lv_obj_set_flex_grow(inner, 1);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(inner, 16, 0);
    lv_obj_set_layout(inner, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(inner, 6, 0);
    lv_obj_add_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(inner, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(inner, LV_SCROLLBAR_MODE_AUTO);

    s_detail_title = lv_label_create(inner);
    lv_label_set_text(s_detail_title, "Tap REFRESH to fetch this feed.");
    lv_label_set_long_mode(s_detail_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail_title, LV_PCT(100));
    lv_obj_set_style_text_font(s_detail_title, PM_LAYOUT_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_detail_title, PM_LAYOUT_COL_FG_BR, 0);

    s_detail_date = lv_label_create(inner);
    lv_label_set_text(s_detail_date, "");
    lv_obj_set_style_text_font(s_detail_date, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_date, PM_LAYOUT_COL_DIM, 0);

    s_detail_link = lv_label_create(inner);
    lv_label_set_text(s_detail_link, "");
    lv_label_set_long_mode(s_detail_link, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_detail_link, LV_PCT(100));
    lv_obj_set_style_text_font(s_detail_link, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_detail_link, PM_LAYOUT_COL_ACCENT, 0);

    s_detail_desc = lv_label_create(inner);
    lv_label_set_text(s_detail_desc, "");
    lv_label_set_long_mode(s_detail_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail_desc, LV_PCT(100));
    lv_obj_set_style_text_font(s_detail_desc, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_detail_desc, PM_LAYOUT_COL_FG, 0);
    lv_obj_set_style_text_line_space(s_detail_desc, 4, 0);

    pm_app_layout_action(&L, "REFRESH",   PM_LAYOUT_COL_ACCENT, _act_refresh);
    pm_app_layout_action(&L, "NEXT FEED", PM_LAYOUT_COL_GOLD,   _act_next_feed);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ─────────────────────────────────────────────
static void _init(void) {
    if (!s_items_mutex) s_items_mutex = xSemaphoreCreateMutex();
    _build_screen();
}

static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    _render();
    if (s_item_count == 0 && !s_fetch_inflight) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            _trigger_fetch(s_current_feed);
        }
    }
}

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) {
    (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 250) return;
    s_last_render = now;
    if (s_dirty || s_fetch_inflight) {
        s_dirty = false;
        _render();
    }
}

static void _exit_(void) {}
static void _deinit(void) {
    if (s_items) { pm_psram_free(s_items); s_items = NULL; }
}

static const pm_app_t _APP = {
    .id           = "rss",
    .display_name = "RSS",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_rss(void) { return &_APP; }
