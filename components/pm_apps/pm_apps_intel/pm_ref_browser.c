// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_ref_browser.c — Shared reference browser
//
//  Index format (index.json):
//    {
//      "entries": [
//        { "id": "001", "name": "Aspirin", "meta": "analgesic" },
//        { "id": "002", "name": "...",     "meta": "..." }
//      ]
//    }
//
//  Documents are at <category>/<id>.json — opaque to this
//  module. Card view shows raw JSON; consumer apps render
//  prettier UI from the parsed JSON if they want.
// ============================================================

#include "pm_ref_browser.h"
#include "pm_hal.h"
#include "pm_nosql.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>

static const char* TAG = "PM_REF";

#define INDEX_BUF_SZ   (32 * 1024)
#define CARD_BUF_SZ    (64 * 1024)
#define VISIBLE_MAX    PM_REF_MAX_RESULTS

struct pm_ref_browser_s {
    pm_ref_config_t cfg;

    pm_ref_entry_t* all;          // PSRAM, size = PM_REF_MAX_RESULTS
    int             all_count;

    int             visible[VISIBLE_MAX];   // indices into all[]
    int             visible_count;

    char            filter[64];
    int             cursor;        // index into visible[]
    int             card_scroll;

    char*           card_buf;      // PSRAM, NULL when no card open
    bool            card_open;
};

// ─────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────
static char* _slurp_index(const char* category) {
    char* buf = (char*)pm_psram_alloc(INDEX_BUF_SZ);
    if (!buf) return NULL;
    size_t got = pm_nosql_read(category, "index", buf, INDEX_BUF_SZ);
    if (got == 0) { pm_psram_free(buf); return NULL; }
    return buf;
}

static void _parse_index(pm_ref_browser_t* b, const char* json_text) {
    b->all_count = 0;
    cJSON* root = cJSON_Parse(json_text);
    if (!root) return;

    cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return; }

    cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (b->all_count >= PM_REF_MAX_RESULTS) break;
        pm_ref_entry_t* e = &b->all[b->all_count];

        const cJSON* jid   = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON* jname = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON* jmeta = cJSON_GetObjectItemCaseSensitive(item, "meta");

        if (cJSON_IsString(jid))   strncpy(e->id,   jid->valuestring,   sizeof(e->id) - 1);
        if (cJSON_IsString(jname)) strncpy(e->name, jname->valuestring, sizeof(e->name) - 1);
        if (cJSON_IsString(jmeta)) strncpy(e->meta, jmeta->valuestring, sizeof(e->meta) - 1);

        e->id[sizeof(e->id) - 1]     = 0;
        e->name[sizeof(e->name) - 1] = 0;
        e->meta[sizeof(e->meta) - 1] = 0;

        b->all_count++;
    }
    cJSON_Delete(root);
}

static bool _matches_filter(const pm_ref_entry_t* e, const char* f) {
    if (!f || !f[0]) return true;
    return strcasestr(e->name, f) != NULL ||
           strcasestr(e->meta, f) != NULL;
}

static void _rebuild_visible(pm_ref_browser_t* b) {
    b->visible_count = 0;
    for (int i = 0; i < b->all_count && b->visible_count < VISIBLE_MAX; i++) {
        if (_matches_filter(&b->all[i], b->filter)) {
            b->visible[b->visible_count++] = i;
        }
    }
    if (b->cursor >= b->visible_count) b->cursor = b->visible_count - 1;
    if (b->cursor < 0) b->cursor = 0;
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
pm_ref_browser_t* pm_ref_browser_create(const pm_ref_config_t* cfg) {
    if (!cfg) return NULL;
    pm_ref_browser_t* b = (pm_ref_browser_t*)pm_psram_calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->cfg = *cfg;
    b->all = (pm_ref_entry_t*)pm_psram_calloc(PM_REF_MAX_RESULTS,
                                                sizeof(pm_ref_entry_t));
    if (!b->all) { pm_psram_free(b); return NULL; }
    pm_nosql_init(cfg->category);
    return b;
}

void pm_ref_browser_destroy(pm_ref_browser_t* b) {
    if (!b) return;
    if (b->card_buf) pm_psram_free(b->card_buf);
    if (b->all)      pm_psram_free(b->all);
    pm_psram_free(b);
}

void pm_ref_browser_refresh(pm_ref_browser_t* b) {
    if (!b) return;
    char* idx = _slurp_index(b->cfg.category);
    if (idx) {
        _parse_index(b, idx);
        pm_psram_free(idx);
    } else {
        b->all_count = 0;
    }
    _rebuild_visible(b);
    pm_log_i(TAG, "[%s] %d entries", b->cfg.category, b->all_count);
}

void pm_ref_browser_set_filter(pm_ref_browser_t* b, const char* f) {
    if (!b) return;
    if (f) {
        strncpy(b->filter, f, sizeof(b->filter) - 1);
        b->filter[sizeof(b->filter) - 1] = 0;
    } else {
        b->filter[0] = 0;
    }
    _rebuild_visible(b);
}

int pm_ref_browser_visible_count(const pm_ref_browser_t* b) {
    return b ? b->visible_count : 0;
}

const pm_ref_entry_t* pm_ref_browser_visible_at(const pm_ref_browser_t* b,
                                                 int index) {
    if (!b || index < 0 || index >= b->visible_count) return NULL;
    return &b->all[b->visible[index]];
}

int pm_ref_browser_cursor(const pm_ref_browser_t* b) {
    return b ? b->cursor : 0;
}

void pm_ref_browser_cursor_move(pm_ref_browser_t* b, int delta) {
    if (!b || b->visible_count == 0) return;
    b->cursor += delta;
    if (b->cursor < 0) b->cursor = 0;
    if (b->cursor >= b->visible_count) b->cursor = b->visible_count - 1;
}

bool pm_ref_browser_open(pm_ref_browser_t* b) {
    if (!b) return false;
    const pm_ref_entry_t* e = pm_ref_browser_visible_at(b, b->cursor);
    if (!e) return false;

    if (!b->card_buf) {
        b->card_buf = (char*)pm_psram_alloc(CARD_BUF_SZ);
        if (!b->card_buf) return false;
    }
    size_t got = pm_nosql_read(b->cfg.category, e->id,
                                b->card_buf, CARD_BUF_SZ);
    if (got == 0) {
        pm_log_w(TAG, "card '%s' empty/missing", e->id);
        return false;
    }
    b->card_scroll = 0;
    b->card_open   = true;
    return true;
}

const char* pm_ref_browser_card(const pm_ref_browser_t* b) {
    return (b && b->card_open && b->card_buf) ? b->card_buf : "";
}

bool pm_ref_browser_card_visible(const pm_ref_browser_t* b) {
    return b ? b->card_open : false;
}

void pm_ref_browser_close_card(pm_ref_browser_t* b) {
    if (!b) return;
    b->card_open = false;
    // Don't free the buf — reuse it on next open.
}

int pm_ref_browser_card_scroll(const pm_ref_browser_t* b) {
    return b ? b->card_scroll : 0;
}

void pm_ref_browser_card_scroll_set(pm_ref_browser_t* b, int y) {
    if (!b) return;
    if (y < 0) y = 0;
    b->card_scroll = y;
}

// ─────────────────────────────────────────────
//  Screen builder (Phase 16)
//
//  Two-pane layout: entry list on the left, card viewer on
//  the right. List rows are clickable; clicking opens the
//  card. The card pane shows either an empty placeholder
//  or the loaded entry's JSON text.
// ─────────────────────────────────────────────
#include "pm_app_layout.h"

// Persistent widget handles (one set per browser since each app
// has its own browser instance; we cache them via pm_obj user data).
typedef struct {
    pm_ref_browser_t* browser;
    lv_obj_t*         list_panel;   // scrollable column of rows
    lv_obj_t*         card_label;   // the right-side document label
    lv_obj_t*         filter_chip;  // optional "%d entries" chip label
} ref_ui_t;

static ref_ui_t* _ui_get_or_create(pm_ref_browser_t* b) {
    if (!b) return NULL;
    // We squat one pointer's worth of space at end of pm_ref_browser_t.
    // Since we can't extend the struct without breaking ABI, allocate
    // on demand and stash in NoSQL? Simpler: a static map of {b -> ui}.
    // For now: keep one ui per browser via a small static cache.
    enum { CACHE_N = 8 };
    static ref_ui_t s_cache[CACHE_N];
    static int      s_used = 0;
    for (int i = 0; i < s_used; i++) {
        if (s_cache[i].browser == b) return &s_cache[i];
    }
    if (s_used >= CACHE_N) return NULL;
    ref_ui_t* u = &s_cache[s_used++];
    u->browser = b;
    return u;
}

static void _row_clicked(lv_event_t* e) {
    pm_ref_browser_t* b = (pm_ref_browser_t*)lv_event_get_user_data(e);
    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (!b || idx < 0 || idx >= b->visible_count) return;
    b->cursor = idx;
    if (pm_ref_browser_open(b)) {
        ref_ui_t* u = _ui_get_or_create(b);
        if (u && u->card_label) {
            lv_label_set_text(u->card_label, pm_ref_browser_card(b));
        }
    }
}

static void _populate_list(pm_ref_browser_t* b, ref_ui_t* u) {
    if (!b || !u || !u->list_panel) return;
    lv_obj_clean(u->list_panel);

    if (b->visible_count == 0) {
        lv_obj_t* empty = lv_label_create(u->list_panel);
        lv_label_set_text(empty,
            "No entries.\nPopulate /sd/nosql/<category>/ on host.");
        lv_obj_set_style_text_font(empty, PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(empty, PM_LAYOUT_COL_DIM, 0);
        lv_obj_set_style_pad_all(empty, 12, 0);
        return;
    }

    for (int i = 0; i < b->visible_count; i++) {
        const pm_ref_entry_t* e = pm_ref_browser_visible_at(b, i);
        if (!e) continue;
        lv_obj_t* row = lv_obj_create(u->list_panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, PM_LAYOUT_COL_BG3, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void*)(intptr_t)i);
        lv_obj_add_event_cb(row, _row_clicked, LV_EVENT_CLICKED, b);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, e->name);
        lv_obj_set_style_text_font(name, PM_LAYOUT_FONT_TEXT, 0);
        lv_obj_set_style_text_color(name, PM_LAYOUT_COL_FG_BR, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, LV_PCT(100));

        if (e->meta[0]) {
            lv_obj_t* meta = lv_label_create(row);
            lv_label_set_text(meta, e->meta);
            lv_obj_set_style_text_font(meta, PM_LAYOUT_FONT_LABEL, 0);
            lv_obj_set_style_text_color(meta, PM_LAYOUT_COL_DIM, 0);
        }
    }
}

lv_obj_t* pm_ref_browser_build_screen(pm_ref_browser_t* b) {
    if (!b) return NULL;
    ref_ui_t* u = _ui_get_or_create(b);

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, b->cfg.title ? b->cfg.title : "REFERENCE");

    char chip[24];
    snprintf(chip, sizeof(chip), "%d ENTRIES", b->visible_count);
    lv_obj_t* clbl = pm_app_layout_chip(&L, chip, PM_LAYOUT_COL_ACCENT);
    if (u) u->filter_chip = clbl;

    pm_app_layout_content(&L);

    // Left: list pane
    lv_obj_t* left = pm_app_layout_pane(&L, 320, "ENTRIES");
    lv_obj_t* list = lv_obj_create(left);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    if (u) u->list_panel = list;

    // Right: card viewer (flex grow)
    lv_obj_t* right = pm_app_layout_pane(&L, 0, "DOCUMENT");
    lv_obj_t* card_scroll = lv_obj_create(right);
    lv_obj_remove_style_all(card_scroll);
    lv_obj_set_width(card_scroll, LV_PCT(100));
    lv_obj_set_flex_grow(card_scroll, 1);
    lv_obj_set_style_bg_opa(card_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(card_scroll, 12, 0);
    lv_obj_add_flag(card_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(card_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card_scroll, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* card = lv_label_create(card_scroll);
    lv_label_set_text(card,
        "Select an entry from the left to view it here.");
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_text_font(card, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(card, PM_LAYOUT_COL_FG, 0);
    lv_label_set_long_mode(card, LV_LABEL_LONG_WRAP);
    if (u) u->card_label = card;

    _populate_list(b, u);

    return pm_app_layout_end(&L);
}

void pm_ref_browser_sync_ui(pm_ref_browser_t* b) {
    if (!b) return;
    ref_ui_t* u = _ui_get_or_create(b);
    if (!u) return;
    _populate_list(b, u);
    if (u->filter_chip) {
        char chip[24];
        snprintf(chip, sizeof(chip), "%d ENTRIES", b->visible_count);
        lv_label_set_text(u->filter_chip, chip);
    }
}
