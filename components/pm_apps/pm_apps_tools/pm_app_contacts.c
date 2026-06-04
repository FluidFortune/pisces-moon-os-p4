// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_contacts.c — Address book reader
//
//  Storage compatibility with the S3 build:
//
//    NoSQL category "contacts"
//      title   = display name
//      content = "phone|email|note"  (pipe-delimited record)
//
//  Plus a CSV fallback if nosql is empty:
//
//    /sd/contacts.csv with columns
//      Name,Phone,Email,Note
//      (commas in fields can be escaped as "\,"; quotes are
//       optional — we accept either form.)
//
//  Editing is intentionally not in this first cut. Adding write
//  support requires the same text-input flow notepad uses (LVGL
//  textarea + pm_ui_keyboard) replicated three times per record,
//  which is a real chunk of work. The reader is the foundation;
//  editor lands in a follow-up.
// ============================================================

#include "pm_app_contacts.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static const char* TAG = "PM_CONTACTS";

#define MAX_CONTACTS     128
#define NAME_LEN         48
#define FIELD_LEN        96
#define NOTE_LEN         256
#define VISIBLE_ROWS     20
#define CSV_PATH         "/sd/contacts.csv"
#define NOSQL_CAT        "contacts"

typedef struct {
    char name[NAME_LEN];
    char phone[FIELD_LEN];
    char email[FIELD_LEN];
    char note[NOTE_LEN];
} contact_t;

static contact_t* s_contacts = NULL;   // PSRAM
static int        s_count    = 0;
static int        s_selected = 0;

// UI handles
static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_list_box    = NULL;
static lv_obj_t* s_name_lbl    = NULL;
static lv_obj_t* s_phone_lbl   = NULL;
static lv_obj_t* s_email_lbl   = NULL;
static lv_obj_t* s_note_lbl    = NULL;
static lv_obj_t* s_chip_count  = NULL;
static lv_obj_t* s_chip_source = NULL;
static lv_obj_t* s_stat_count  = NULL;
static lv_obj_t* s_stat_phone  = NULL;
static lv_obj_t* s_stat_email  = NULL;
static bool      s_built       = false;

typedef struct {
    lv_obj_t* row;
    lv_obj_t* name_lbl;
    lv_obj_t* hint_lbl;
} row_ui_t;
static row_ui_t s_rows[VISIBLE_ROWS];
static int      s_rows_created = 0;

typedef enum { SRC_NONE, SRC_NOSQL, SRC_CSV } source_t;
static source_t s_source = SRC_NONE;

// ── Parsing ──────────────────────────────────────────────

// Pipe-split a "phone|email|note" payload into a contact.
static void _split_pipes(const char* payload, contact_t* c) {
    if (!payload || !c) return;
    const char* p = payload;
    const char* q;
    int field = 0;
    while (*p && field < 3) {
        q = strchr(p, '|');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        char*  dst;
        size_t cap;
        if (field == 0)      { dst = c->phone; cap = sizeof(c->phone); }
        else if (field == 1) { dst = c->email; cap = sizeof(c->email); }
        else                 { dst = c->note;  cap = sizeof(c->note);  }
        size_t copy = len < cap - 1 ? len : cap - 1;
        memcpy(dst, p, copy);
        dst[copy] = '\0';
        if (!q) break;
        p = q + 1;
        field++;
    }
}

// Parse one CSV row. Accepts:
//    plain values:          a,b,c,d
//    quoted values:         "a","b","c","d with, comma"
//    escaped commas:        a\,b,c
//
// Mutates `line` in place; the contact's fields are copied out.
static void _parse_csv_line(char* line, contact_t* c) {
    if (!line || !c) return;
    char* fields[4] = { c->name, c->phone, c->email, c->note };
    size_t caps[4]  = { sizeof(c->name), sizeof(c->phone),
                          sizeof(c->email), sizeof(c->note) };

    char buf[NOTE_LEN];
    int  fi = 0;
    int  bi = 0;
    bool in_quote = false;

    for (int i = 0; line[i]; i++) {
        char ch = line[i];
        if (ch == '\r' || ch == '\n') continue;
        if (ch == '"') { in_quote = !in_quote; continue; }
        if (ch == '\\' && line[i + 1] == ',') {
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = ',';
            i++;
            continue;
        }
        if (ch == ',' && !in_quote) {
            buf[bi] = '\0';
            if (fi < 4) {
                strncpy(fields[fi], buf, caps[fi] - 1);
                fields[fi][caps[fi] - 1] = '\0';
            }
            fi++; bi = 0;
            continue;
        }
        if (bi < (int)sizeof(buf) - 1) buf[bi++] = ch;
    }
    buf[bi] = '\0';
    if (fi < 4) {
        strncpy(fields[fi], buf, caps[fi] - 1);
        fields[fi][caps[fi] - 1] = '\0';
    }
}

// Load from NoSQL — iterate keys in the contacts category. Returns
// the count appended.
//
// We don't have a perfect "iterate keys in category" API surfaced,
// so we try a small index of expected key names. Real apps use
// pm_nosql_list which we'll fall back to if available; for now,
// we accept that contacts created on S3 with arbitrary keys may
// not load here — the CSV path is the reliable transfer route.
//
// (Hardening: once pm_nosql_list is wired into pm_nosql.h on P4,
// this loop just iterates the returned keys.)
static int _load_nosql(void) {
    // Best-effort: try sequentially numbered keys "0".."MAX-1".
    int loaded = 0;
    for (int i = 0; i < MAX_CONTACTS && s_count < MAX_CONTACTS; i++) {
        char key[12];
        snprintf(key, sizeof(key), "%d", i);

        // Title via metadata isn't exposed; read content directly.
        char payload[NOTE_LEN + FIELD_LEN * 2 + 8] = {0};
        size_t got = pm_nosql_read(NOSQL_CAT, key,
                                    payload, sizeof(payload) - 1);
        if (got == 0) continue;
        payload[got] = 0;

        contact_t* c = &s_contacts[s_count++];
        memset(c, 0, sizeof(*c));
        // Name lives in the key when we use numeric keys — but
        // here we treat the key as the title. Real S3 stored
        // title separately. Fall back to "Contact <n>".
        snprintf(c->name, sizeof(c->name), "Contact %d", i);
        _split_pipes(payload, c);
        loaded++;
    }
    return loaded;
}

static int _load_csv(void) {
    int loaded = 0;
    PM_SPI_TAKE("contacts_csv") {
        pm_file_t* f = pm_file_open(CSV_PATH, PM_FILE_READ);
        if (f) {
            size_t sz = pm_file_size(f);
            if (sz > 256 * 1024) sz = 256 * 1024;
            char* buf = (char*)pm_psram_alloc(sz + 1);
            if (buf) {
                size_t got = pm_file_read(f, buf, sz);
                buf[got] = '\0';
                // Walk by line.
                char* line = buf;
                bool header_skipped = false;
                while (*line) {
                    char* eol = strpbrk(line, "\r\n");
                    if (eol) { *eol = '\0'; }
                    if (line[0] && s_count < MAX_CONTACTS) {
                        // Skip header if the first column literally
                        // reads "name" or "Name".
                        if (!header_skipped) {
                            header_skipped = true;
                            if (strncasecmp(line, "name", 4) == 0) {
                                goto next;
                            }
                        }
                        contact_t* c = &s_contacts[s_count++];
                        memset(c, 0, sizeof(*c));
                        _parse_csv_line(line, c);
                        loaded++;
                    }
                  next:
                    if (!eol) break;
                    line = eol + 1;
                    // Eat \r\n / \n\r combos.
                    while (*line == '\r' || *line == '\n') line++;
                }
                pm_psram_free(buf);
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return loaded;
}

static int _name_cmp(const void* a, const void* b) {
    return strcasecmp(((const contact_t*)a)->name,
                       ((const contact_t*)b)->name);
}

static void _load_all(void) {
    s_count = 0;
    if (!s_contacts) {
        s_contacts = (contact_t*)pm_psram_alloc(sizeof(contact_t) * MAX_CONTACTS);
        if (!s_contacts) {
            pm_log_e(TAG, "PSRAM alloc failed");
            return;
        }
    }
    memset(s_contacts, 0, sizeof(contact_t) * MAX_CONTACTS);
    s_source = SRC_NONE;

    int n = _load_nosql();
    if (n > 0) {
        s_source = SRC_NOSQL;
    } else {
        n = _load_csv();
        if (n > 0) s_source = SRC_CSV;
    }
    if (s_count > 1) qsort(s_contacts, s_count, sizeof(contact_t), _name_cmp);
    s_selected = 0;
    pm_log_i(TAG, "loaded %d contacts (source=%d)", s_count, (int)s_source);
}

// ── Detail pane ──────────────────────────────────────────
static void _show_detail(int idx) {
    if (idx < 0 || idx >= s_count) {
        if (s_name_lbl)  lv_label_set_text(s_name_lbl,  "(no contact selected)");
        if (s_phone_lbl) lv_label_set_text(s_phone_lbl, "—");
        if (s_email_lbl) lv_label_set_text(s_email_lbl, "—");
        if (s_note_lbl)  lv_label_set_text(s_note_lbl,  "");
        return;
    }
    const contact_t* c = &s_contacts[idx];
    if (s_name_lbl)  lv_label_set_text(s_name_lbl,  c->name[0]  ? c->name  : "(unnamed)");
    if (s_phone_lbl) lv_label_set_text(s_phone_lbl, c->phone[0] ? c->phone : "—");
    if (s_email_lbl) lv_label_set_text(s_email_lbl, c->email[0] ? c->email : "—");
    if (s_note_lbl)  lv_label_set_text(s_note_lbl,  c->note[0]  ? c->note  : "");
}

// ── Row click ────────────────────────────────────────────
static void _on_row_clicked(lv_event_t* e) {
    lv_obj_t* row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx >= 0 && idx < s_count) {
        s_selected = idx;
        _show_detail(idx);
    }
}

// ── Row builder ──────────────────────────────────────────
static void _build_row(int idx, lv_obj_t* parent) {
    row_ui_t* r = &s_rows[idx];
    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, 38);
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

    r->name_lbl = lv_label_create(r->row);
    lv_label_set_text(r->name_lbl, "—");
    lv_label_set_long_mode(r->name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->name_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->name_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(r->name_lbl, PM_LAYOUT_COL_FG_BR, 0);

    r->hint_lbl = lv_label_create(r->row);
    lv_label_set_text(r->hint_lbl, "—");
    lv_label_set_long_mode(r->hint_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->hint_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(r->hint_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(r->hint_lbl, PM_LAYOUT_COL_DIM, 0);
}

static void _render_list(void) {
    int with_phone = 0, with_email = 0;
    int visible = 0;
    for (int i = 0; i < s_count && i < VISIBLE_ROWS; i++) {
        if (visible >= s_rows_created) {
            _build_row(s_rows_created, s_list_box);
            s_rows_created++;
        }
        row_ui_t* r = &s_rows[visible];
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(r->row, (void*)(intptr_t)i);
        lv_label_set_text(r->name_lbl, s_contacts[i].name);
        // Show whichever contact field looks most "primary".
        const char* hint = s_contacts[i].phone[0] ? s_contacts[i].phone
                         : s_contacts[i].email[0] ? s_contacts[i].email
                         : "—";
        lv_label_set_text(r->hint_lbl, hint);
        if (s_contacts[i].phone[0]) with_phone++;
        if (s_contacts[i].email[0]) with_email++;
        visible++;
    }
    for (int i = visible; i < s_rows_created; i++) {
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_chip_count) {
        char b[16]; snprintf(b, sizeof(b), "%d", s_count);
        lv_label_set_text(s_chip_count, b);
    }
    if (s_chip_source) {
        const char* sx = s_source == SRC_NOSQL ? "NOSQL"
                       : s_source == SRC_CSV   ? "CSV"
                       :                         "NONE";
        lv_label_set_text(s_chip_source, sx);
    }
    if (s_stat_count) { char b[12]; snprintf(b, sizeof(b), "%d", s_count);      lv_label_set_text(s_stat_count, b); }
    if (s_stat_phone) { char b[12]; snprintf(b, sizeof(b), "%d", with_phone);   lv_label_set_text(s_stat_phone, b); }
    if (s_stat_email) { char b[12]; snprintf(b, sizeof(b), "%d", with_email);   lv_label_set_text(s_stat_email, b); }

    _show_detail(s_selected);
}

// ── Actions ──────────────────────────────────────────────
static void _act_reload(lv_event_t* e) { (void)e; _load_all(); _render_list(); }

// ── Build ────────────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;
    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "CONTACTS");

    s_chip_count  = pm_app_layout_chip(&L, "0",   PM_LAYOUT_COL_ACCENT);
    s_chip_source = pm_app_layout_chip(&L, "NONE", PM_LAYOUT_COL_DIM);

    pm_app_layout_stats_row(&L, 3);
    s_stat_count = pm_app_layout_stat(&L, "TOTAL",      "0");
    s_stat_phone = pm_app_layout_stat(&L, "W/ PHONE",   "0");
    s_stat_email = pm_app_layout_stat(&L, "W/ EMAIL",   "0");

    pm_app_layout_content(&L);

    // Left: list
#if PM_BOARD_LCD_H_RES <= 800
    int list_w = 280;
#else
    int list_w = 380;
#endif
    lv_obj_t* lp = pm_app_layout_pane(&L, list_w, "DIRECTORY");
    s_list_box = lv_obj_create(lp);
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

    // Right: detail
    lv_obj_t* dp = pm_app_layout_pane(&L, 0, "DETAIL");
    lv_obj_set_style_pad_all(dp, 0, 0);

    lv_obj_t* inner = lv_obj_create(dp);
    lv_obj_remove_style_all(inner);
    lv_obj_set_width(inner, LV_PCT(100));
    lv_obj_set_flex_grow(inner, 1);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(inner, 18, 0);
    lv_obj_set_layout(inner, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(inner, 8, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    s_name_lbl = lv_label_create(inner);
    lv_label_set_text(s_name_lbl, "(no contact selected)");
    lv_label_set_long_mode(s_name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_name_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_name_lbl, PM_LAYOUT_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_name_lbl, PM_LAYOUT_COL_ACCENT, 0);

    // Phone row
    lv_obj_t* p_row = lv_obj_create(inner);
    lv_obj_remove_style_all(p_row);
    lv_obj_set_width(p_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(p_row, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(p_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(p_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p_row, 12, 0);
    lv_obj_t* p_tag = lv_label_create(p_row);
    lv_label_set_text(p_tag, "PHONE");
    lv_obj_set_style_text_font(p_tag, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(p_tag, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(p_tag, 60);
    s_phone_lbl = lv_label_create(p_row);
    lv_label_set_text(s_phone_lbl, "—");
    lv_obj_set_style_text_font(s_phone_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_phone_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_flex_grow(s_phone_lbl, 1);

    // Email row
    lv_obj_t* e_row = lv_obj_create(inner);
    lv_obj_remove_style_all(e_row);
    lv_obj_set_width(e_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(e_row, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(e_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(e_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(e_row, 12, 0);
    lv_obj_t* e_tag = lv_label_create(e_row);
    lv_label_set_text(e_tag, "EMAIL");
    lv_obj_set_style_text_font(e_tag, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(e_tag, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_width(e_tag, 60);
    s_email_lbl = lv_label_create(e_row);
    lv_label_set_text(s_email_lbl, "—");
    lv_obj_set_style_text_font(s_email_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_email_lbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_flex_grow(s_email_lbl, 1);

    // Note (wrapped)
    lv_obj_t* note_tag = lv_label_create(inner);
    lv_label_set_text(note_tag, "NOTE");
    lv_obj_set_style_text_font(note_tag, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(note_tag, PM_LAYOUT_COL_DIM, 0);
    s_note_lbl = lv_label_create(inner);
    lv_label_set_text(s_note_lbl, "");
    lv_label_set_long_mode(s_note_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_note_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_note_lbl, PM_LAYOUT_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_note_lbl, PM_LAYOUT_COL_FG_BR, 0);

    pm_app_layout_action(&L, "RELOAD", PM_LAYOUT_COL_ACCENT, _act_reload);

    s_screen = pm_app_layout_end(&L);
    s_built  = true;
}

// ── Lifecycle ────────────────────────────────────────────
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_built) _build_screen();
    if (s_screen) lv_screen_load(s_screen);
    if (!pm_sd_mounted()) pm_sd_mount();
    if (s_count == 0) _load_all();
    _render_list();
}
static void _exit_(void) {}
static void _deinit(void) {
    if (s_contacts) { pm_psram_free(s_contacts); s_contacts = NULL; }
}

static const pm_app_t _APP = {
    .id           = "contacts",
    .display_name = "CONTACTS",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_contacts(void) { return &_APP; }
