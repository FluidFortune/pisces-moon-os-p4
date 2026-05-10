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
