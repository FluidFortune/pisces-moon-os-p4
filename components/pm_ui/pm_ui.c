// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_ui.c — Pisces UI Kit implementation
// ============================================================

#include "pm_ui.h"
#include "pm_hal.h"
#include "pm_launcher.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static const char* TAG = "PM_UI";

// ── Cached styles, one-time init ─────────────────────────────
static bool s_theme_inited = false;
static lv_style_t s_st_screen;
static lv_style_t s_st_card;
static lv_style_t s_st_titlebar;
static lv_style_t s_st_btn;
static lv_style_t s_st_btn_pressed;
static lv_style_t s_st_chip;
static lv_style_t s_st_list;
static lv_style_t s_st_meter_main;
static lv_style_t s_st_meter_indic;

void pm_ui_theme_init(void) {
    if (s_theme_inited) return;
    s_theme_inited = true;

    // Screen base
    lv_style_init(&s_st_screen);
    lv_style_set_bg_color(&s_st_screen, PM_C_BG);
    lv_style_set_bg_opa  (&s_st_screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_screen, PM_C_FG);
    lv_style_set_pad_all (&s_st_screen, 8);

    // Card
    lv_style_init(&s_st_card);
    lv_style_set_bg_color(&s_st_card, PM_C_BG_2);
    lv_style_set_bg_opa  (&s_st_card, LV_OPA_COVER);
    lv_style_set_border_color(&s_st_card, PM_C_BORDER);
    lv_style_set_border_width(&s_st_card, 1);
    lv_style_set_radius      (&s_st_card, 8);
    lv_style_set_pad_all     (&s_st_card, 10);
    lv_style_set_text_color  (&s_st_card, PM_C_FG);

    // Titlebar
    lv_style_init(&s_st_titlebar);
    lv_style_set_bg_color(&s_st_titlebar, PM_C_BG_3);
    lv_style_set_bg_opa  (&s_st_titlebar, LV_OPA_COVER);
    lv_style_set_border_side(&s_st_titlebar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_st_titlebar, PM_C_BORDER);
    lv_style_set_border_width(&s_st_titlebar, 1);
    lv_style_set_pad_hor (&s_st_titlebar, 12);
    lv_style_set_pad_ver (&s_st_titlebar, 6);
    lv_style_set_radius  (&s_st_titlebar, 0);

    // Button
    lv_style_init(&s_st_btn);
    lv_style_set_bg_color(&s_st_btn, PM_C_ACCENT);
    lv_style_set_bg_opa  (&s_st_btn, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_btn, PM_C_BG);
    lv_style_set_radius  (&s_st_btn, 6);
    lv_style_set_pad_hor (&s_st_btn, 14);
    lv_style_set_pad_ver (&s_st_btn, 8);

    lv_style_init(&s_st_btn_pressed);
    lv_style_set_bg_color(&s_st_btn_pressed, PM_C_ACCENT_2);

    // Chip
    lv_style_init(&s_st_chip);
    lv_style_set_radius  (&s_st_chip, LV_RADIUS_CIRCLE);
    lv_style_set_pad_hor (&s_st_chip, 10);
    lv_style_set_pad_ver (&s_st_chip, 2);
    lv_style_set_bg_opa  (&s_st_chip, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_chip, PM_C_BG);

    // List
    lv_style_init(&s_st_list);
    lv_style_set_bg_color(&s_st_list, PM_C_BG_2);
    lv_style_set_bg_opa  (&s_st_list, LV_OPA_COVER);
    lv_style_set_border_color(&s_st_list, PM_C_BORDER);
    lv_style_set_border_width(&s_st_list, 1);
    lv_style_set_radius      (&s_st_list, 6);
    lv_style_set_pad_all     (&s_st_list, 4);
    lv_style_set_text_color  (&s_st_list, PM_C_FG);

    // Meter bar
    lv_style_init(&s_st_meter_main);
    lv_style_set_bg_color(&s_st_meter_main, PM_C_BG_3);
    lv_style_set_bg_opa  (&s_st_meter_main, LV_OPA_COVER);
    lv_style_set_radius  (&s_st_meter_main, 4);
    lv_style_set_border_width(&s_st_meter_main, 0);

    lv_style_init(&s_st_meter_indic);
    lv_style_set_bg_color(&s_st_meter_indic, PM_C_ACCENT);
    lv_style_set_bg_opa  (&s_st_meter_indic, LV_OPA_COVER);
    lv_style_set_radius  (&s_st_meter_indic, 4);

    pm_log_i(TAG, "theme initialized");
}

// ─────────────────────────────────────────────
//  Screen
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_screen(void) {
    pm_ui_theme_init();
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &s_st_screen, 0);
    lv_obj_set_layout(scr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    return scr;
}

// ─────────────────────────────────────────────
//  Titlebar
// ─────────────────────────────────────────────
static void _back_default_cb(lv_event_t* e) {
    (void)e;
    pm_launcher_back_from_app();   // leave foreground app, then return
}

lv_obj_t* pm_ui_titlebar(lv_obj_t* parent, const char* title,
                          lv_event_cb_t back_cb, void* back_user) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, &s_st_titlebar, 0);
    lv_obj_set_size(bar, LV_PCT(100), 40);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // back button
    lv_obj_t* back = lv_button_create(bar);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 40, 28);
    lv_obj_set_style_bg_color(back, PM_C_BG_3, 0);
    lv_obj_set_style_bg_opa  (back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius  (back, 4, 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, PM_C_ACCENT, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, back_cb ? back_cb : _back_default_cb,
                         LV_EVENT_CLICKED, back_user);

    // title label
    lv_obj_t* tl = lv_label_create(bar);
    lv_label_set_text(tl, title ? title : "");
    lv_obj_set_style_text_color(tl, PM_C_FG, 0);
    lv_obj_set_style_pad_left  (tl, 12, 0);

    return bar;
}

// ─────────────────────────────────────────────
//  Card
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_card(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &s_st_card, 0);
    lv_obj_set_layout(c, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    return c;
}

// ─────────────────────────────────────────────
//  Button
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_button(lv_obj_t* parent, const char* label,
                        lv_event_cb_t cb, void* user) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &s_st_btn, 0);
    lv_obj_add_style(btn, &s_st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* lb = lv_label_create(btn);
    lv_label_set_text(lb, label ? label : "");
    lv_obj_center(lb);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    return btn;
}

// ─────────────────────────────────────────────
//  Chip
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_chip(lv_obj_t* parent, const char* text, lv_color_t color) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &s_st_chip, 0);
    lv_obj_set_style_bg_color(c, color, 0);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_width (c, LV_SIZE_CONTENT);
    lv_obj_t* lb = lv_label_create(c);
    lv_label_set_text(lb, text ? text : "");
    return c;
}

// ─────────────────────────────────────────────
//  KV row
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_kv_row(lv_obj_t* parent, const char* key, const char* initial) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 4, 0);

    lv_obj_t* k = lv_label_create(row);
    lv_label_set_text(k, key ? key : "");
    lv_obj_set_style_text_color(k, PM_C_FG_DIM, 0);

    lv_obj_t* v = lv_label_create(row);
    lv_label_set_text(v, initial ? initial : "—");
    lv_obj_set_style_text_color(v, PM_C_FG, 0);
    return v;
}

// ─────────────────────────────────────────────
//  Status dot
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_status_dot(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 10, 10);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa  (d, LV_OPA_COVER, 0);
    lv_obj_set_style_radius  (d, LV_RADIUS_CIRCLE, 0);
    return d;
}

// ─────────────────────────────────────────────
//  List
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_list(lv_obj_t* parent) {
    lv_obj_t* l = lv_list_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_add_style(l, &s_st_list, 0);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

// ─────────────────────────────────────────────
//  Meter bar
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_meter_bar(lv_obj_t* parent, int min, int max) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &s_st_meter_main, 0);
    lv_obj_add_style(b, &s_st_meter_indic, LV_PART_INDICATOR);
    lv_obj_set_size(b, LV_PCT(100), 8);
    lv_bar_set_range(b, min, max);
    return b;
}

// ─────────────────────────────────────────────
//  Keypad
// ─────────────────────────────────────────────
typedef struct {
    pm_ui_keypad_cb cb;
    void*            user;
} kp_data_t;

static void _kp_btn_cb(lv_event_t* e) {
    kp_data_t* d = (kp_data_t*)lv_event_get_user_data(e);
    lv_obj_t* btn = lv_event_get_target(e);
    char ch = (char)(intptr_t)lv_obj_get_user_data(btn);
    if (d && d->cb) d->cb(ch, d->user);
}

lv_obj_t* pm_ui_keypad(lv_obj_t* parent, const char* layout,
                        pm_ui_keypad_cb cb, void* user) {
    lv_obj_t* pad = lv_obj_create(parent);
    lv_obj_remove_style_all(pad);
    lv_obj_set_layout(pad, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(pad, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(pad, 6, 0);
    lv_obj_set_style_pad_row(pad, 6, 0);

    kp_data_t* d = (kp_data_t*)pm_psram_calloc(1, sizeof(kp_data_t));
    if (d) { d->cb = cb; d->user = user; }

    const char* p = layout ? layout : "";
    while (*p) {
        lv_obj_t* row = lv_obj_create(pad);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_PCT(25));
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        while (*p && *p != '\n') {
            char label[2] = { *p, 0 };
            lv_obj_t* btn = pm_ui_button(row, label, _kp_btn_cb, d);
            lv_obj_set_user_data(btn, (void*)(intptr_t)*p);
            lv_obj_set_flex_grow(btn, 1);
            lv_obj_set_height(btn, LV_PCT(100));
            p++;
        }
        if (*p == '\n') p++;
    }
    return pad;
}

// ─────────────────────────────────────────────
//  Log panel — append-only scrollback
// ─────────────────────────────────────────────
struct pm_ui_log_s {
    lv_obj_t* container;
    lv_obj_t* label;
    char*     text;
    size_t    text_len;
    size_t    text_cap;
};

#define PM_UI_LOG_CAP  (16 * 1024)

pm_ui_log_t* pm_ui_log_create(lv_obj_t* parent) {
    pm_ui_log_t* l = (pm_ui_log_t*)pm_psram_calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->text_cap = PM_UI_LOG_CAP;
    l->text     = (char*)pm_psram_alloc(l->text_cap);
    if (!l->text) { pm_psram_free(l); return NULL; }
    l->text[0]  = 0;
    l->text_len = 0;

    l->container = lv_obj_create(parent);
    lv_obj_remove_style_all(l->container);
    lv_obj_add_style(l->container, &s_st_card, 0);
    lv_obj_set_size(l->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scroll_dir(l->container, LV_DIR_VER);

    l->label = lv_label_create(l->container);
    lv_obj_set_width(l->label, LV_PCT(100));
    lv_label_set_text(l->label, "");
    lv_obj_set_style_text_color(l->label, PM_C_FG, 0);
    lv_label_set_long_mode(l->label, LV_LABEL_LONG_WRAP);
    return l;
}

void pm_ui_log_append(pm_ui_log_t* l, const char* line) {
    if (!l || !line) return;
    size_t n = strlen(line);
    if (l->text_len + n + 2 >= l->text_cap) {
        size_t half = l->text_cap / 2;
        memmove(l->text, l->text + half, l->text_len - half);
        l->text_len -= half;
        l->text[l->text_len] = 0;
    }
    memcpy(l->text + l->text_len, line, n);
    l->text_len += n;
    l->text[l->text_len++] = '\n';
    l->text[l->text_len]   = 0;
    lv_label_set_text(l->label, l->text);
    lv_obj_scroll_to_y(l->container, lv_obj_get_scroll_bottom(l->container), LV_ANIM_OFF);
}

void pm_ui_log_clear(pm_ui_log_t* l) {
    if (!l) return;
    l->text[0]  = 0;
    l->text_len = 0;
    lv_label_set_text(l->label, "");
}

lv_obj_t* pm_ui_log_obj(pm_ui_log_t* l) { return l ? l->container : NULL; }

// ─────────────────────────────────────────────
//  Grid
// ─────────────────────────────────────────────
lv_obj_t* pm_ui_grid(lv_obj_t* parent, int rows, int cols) {
    lv_obj_t* g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(g, 6, 0);
    lv_obj_set_style_pad_row(g, 6, 0);
    lv_obj_set_style_pad_column(g, 6, 0);

    static int32_t rdefs[16];
    static int32_t cdefs[16];
    if (rows > 15) rows = 15;
    if (cols > 15) cols = 15;
    for (int i = 0; i < rows; i++) rdefs[i] = LV_GRID_FR(1);
    rdefs[rows] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < cols; i++) cdefs[i] = LV_GRID_FR(1);
    cdefs[cols] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(g, cdefs, rdefs);
    lv_obj_set_layout(g, LV_LAYOUT_GRID);
    return g;
}

// ─────────────────────────────────────────────
//  Default app screen
// ─────────────────────────────────────────────
//
//  Layout: titlebar on top, single card with a status label
//  centered. Apps store the screen handle and use
//  pm_ui_default_screen_set_status() to refresh.
//
//  This is the minimum a new app should ship — it boots into
//  a real LVGL screen the user can navigate back from. As apps
//  mature, they replace this with a custom build_screen.
//
static lv_obj_t* _find_status_label(lv_obj_t* scr) {
    if (!scr) return NULL;
    if (lv_obj_get_child_count(scr) < 2) return NULL;
    lv_obj_t* card = lv_obj_get_child(scr, 1);
    if (!card || lv_obj_get_child_count(card) < 1) return NULL;
    return lv_obj_get_child(card, 0);
}

lv_obj_t* pm_ui_default_screen(const char* app_title, const char* status_text) {
    lv_obj_t* scr = pm_ui_screen();
    pm_ui_titlebar(scr, app_title ? app_title : "APP", NULL, NULL);

    lv_obj_t* card = pm_ui_card(scr);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, status_text ? status_text : "Ready");
    lv_obj_set_style_text_color(lab, PM_C_FG, 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, LV_PCT(90));
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    return scr;
}

void pm_ui_default_screen_set_status(lv_obj_t* scr, const char* text) {
    lv_obj_t* lab = _find_status_label(scr);
    if (lab) lv_label_set_text(lab, text ? text : "");
}

// ─────────────────────────────────────────────
//  On-screen QWERTY keyboard
// ─────────────────────────────────────────────
//
// Wraps LVGL's lv_keyboard. When attached to a textarea, LVGL owns text
// insertion; otherwise keystrokes are fanned into pm_input for raw-key apps.
//
#include "pm_input.h"

struct pm_ui_keyboard_s {
    lv_obj_t* kb;
    lv_obj_t* attached_ta;
};

static void _kb_event_cb(lv_event_t* e) {
    lv_obj_t*    kb   = lv_event_get_target(e);
    lv_event_code_t c = lv_event_get_code(e);
    pm_ui_keyboard_t* state = (pm_ui_keyboard_t*)lv_event_get_user_data(e);
    if (c == LV_EVENT_VALUE_CHANGED) {
        if (state && state->attached_ta) return;
        uint32_t btn_id = lv_keyboard_get_selected_button(kb);
        if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE) return;
        const char* txt = lv_keyboard_get_button_text(kb, btn_id);
        if (!txt || !*txt) return;
        // Map LVGL "special" labels to pm_input codes
        pm_input_event_t ev = {
            .kind      = PM_INPUT_KEY,
            .source    = PM_INPUT_SRC_VIRTUAL_KEYBOARD,
            .down      = true,
            .timestamp = pm_millis(),
        };
        if      (!strcmp(txt, LV_SYMBOL_BACKSPACE)) ev.code = PM_KEY_BACKSPACE;
        else if (!strcmp(txt, LV_SYMBOL_NEW_LINE))  ev.code = PM_KEY_ENTER;
        else if (!strcmp(txt, LV_SYMBOL_OK))        ev.code = PM_KEY_ENTER;
        else if (!strcmp(txt, LV_SYMBOL_CLOSE))     ev.code = PM_KEY_ESC;
        else if (!strcmp(txt, " "))                  ev.code = PM_KEY_SPACE;
        else if (txt[0] && !txt[1])                  ev.code = (uint32_t)txt[0];
        else return;     // multi-char labels (shift/abc) — handled by LVGL
        pm_input_post(&ev);
    }
}

pm_ui_keyboard_t* pm_ui_keyboard_create(lv_obj_t* parent) {
    pm_ui_theme_init();
    pm_ui_keyboard_t* k = (pm_ui_keyboard_t*)pm_psram_calloc(1, sizeof(*k));
    if (!k) return NULL;
    k->kb = lv_keyboard_create(parent);
    lv_obj_set_size(k->kb, LV_PCT(100), LV_PCT(40));
    lv_obj_align(k->kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color   (k->kb, PM_C_BG_2, LV_PART_MAIN);
    lv_obj_set_style_border_color(k->kb, PM_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(k->kb, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color   (k->kb, PM_C_BG_3, LV_PART_ITEMS);
    lv_obj_set_style_text_color (k->kb, PM_C_FG,    LV_PART_ITEMS);
    lv_obj_set_style_radius     (k->kb, 4,          LV_PART_ITEMS);
    lv_obj_add_event_cb(k->kb, _kb_event_cb, LV_EVENT_VALUE_CHANGED, k);
    lv_obj_add_flag(k->kb, LV_OBJ_FLAG_HIDDEN);   // hidden until shown
    return k;
}

void pm_ui_keyboard_attach(pm_ui_keyboard_t* k, lv_obj_t* ta) {
    if (!k) return;
    k->attached_ta = ta;
    lv_keyboard_set_textarea(k->kb, ta);
}

void pm_ui_keyboard_show(pm_ui_keyboard_t* k) {
    if (!k) return;
    lv_obj_clear_flag(k->kb, LV_OBJ_FLAG_HIDDEN);
}

void pm_ui_keyboard_hide(pm_ui_keyboard_t* k) {
    if (!k) return;
    lv_obj_add_flag(k->kb, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* pm_ui_keyboard_obj(pm_ui_keyboard_t* k) { return k ? k->kb : NULL; }

// ─────────────────────────────────────────────
//  On-screen virtual gamepad
// ─────────────────────────────────────────────
//
// Touch events on each zone post pm_input events. Press = down=true,
// release = down=false, so games can implement hold-to-walk.
//
struct pm_ui_gamepad_s {
    lv_obj_t* container;
};

typedef struct {
    pm_input_kind_t kind;
    uint32_t        code;
} zone_meta_t;

static void _gp_zone_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    zone_meta_t* m = (zone_meta_t*)lv_event_get_user_data(e);
    if (!m) return;
    pm_input_event_t ev = {
        .kind      = m->kind,
        .code      = m->code,
        .source    = PM_INPUT_SRC_VIRTUAL_GAMEPAD,
        .down      = (c == LV_EVENT_PRESSED),
        .timestamp = pm_millis(),
    };
    if (c == LV_EVENT_PRESSED || c == LV_EVENT_RELEASED) {
        pm_input_post(&ev);
    }
}

static lv_obj_t* _gp_make_zone(lv_obj_t* parent, const char* label,
                                 lv_color_t accent,
                                 pm_input_kind_t kind, uint32_t code) {
    lv_obj_t* z = lv_obj_create(parent);
    lv_obj_remove_style_all(z);
    lv_obj_set_size(z, 70, 70);
    lv_obj_set_style_bg_color   (z, PM_C_BG_3, 0);
    lv_obj_set_style_bg_opa     (z, LV_OPA_COVER, 0);
    lv_obj_set_style_radius     (z, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(z, accent, 0);
    lv_obj_set_style_border_width(z, 2, 0);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* lbl = lv_label_create(z);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, accent, 0);
    lv_obj_center(lbl);
    zone_meta_t* m = (zone_meta_t*)pm_psram_calloc(1, sizeof(*m));
    if (m) { m->kind = kind; m->code = code; }
    lv_obj_add_event_cb(z, _gp_zone_evt, LV_EVENT_PRESSED,  m);
    lv_obj_add_event_cb(z, _gp_zone_evt, LV_EVENT_RELEASED, m);
    return z;
}

pm_ui_gamepad_t* pm_ui_gamepad_create(lv_obj_t* parent) {
    pm_ui_theme_init();
    pm_ui_gamepad_t* g = (pm_ui_gamepad_t*)pm_psram_calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->container = lv_obj_create(parent);
    lv_obj_remove_style_all(g->container);
    lv_obj_set_size(g->container, LV_PCT(100), 200);
    lv_obj_align(g->container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g->container, PM_C_BG, 0);
    lv_obj_set_style_bg_opa  (g->container, LV_OPA_70, 0);
    lv_obj_set_style_pad_all (g->container, 10, 0);
    lv_obj_clear_flag(g->container, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: d-pad cross (3x3 grid with center empty)
    lv_obj_t* dpad = lv_obj_create(g->container);
    lv_obj_remove_style_all(dpad);
    lv_obj_set_size(dpad, 220, 180);
    lv_obj_align(dpad, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(dpad, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* up    = _gp_make_zone(dpad, LV_SYMBOL_UP,    PM_C_ACCENT, PM_INPUT_DPAD, PM_DPAD_UP);
    lv_obj_t* down  = _gp_make_zone(dpad, LV_SYMBOL_DOWN,  PM_C_ACCENT, PM_INPUT_DPAD, PM_DPAD_DOWN);
    lv_obj_t* left  = _gp_make_zone(dpad, LV_SYMBOL_LEFT,  PM_C_ACCENT, PM_INPUT_DPAD, PM_DPAD_LEFT);
    lv_obj_t* right = _gp_make_zone(dpad, LV_SYMBOL_RIGHT, PM_C_ACCENT, PM_INPUT_DPAD, PM_DPAD_RIGHT);
    lv_obj_align(up,    LV_ALIGN_TOP_MID,    0, 0);
    lv_obj_align(down,  LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_align(left,  LV_ALIGN_LEFT_MID,   0, 0);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID,  0, 0);

    // Right side: A/B/X/Y diamond
    lv_obj_t* btns = lv_obj_create(g->container);
    lv_obj_remove_style_all(btns);
    lv_obj_set_size(btns, 220, 180);
    lv_obj_align(btns, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* y = _gp_make_zone(btns, "Y", PM_C_ACCENT_2, PM_INPUT_BUTTON, PM_BTN_Y);
    lv_obj_t* a = _gp_make_zone(btns, "A", PM_C_OK,       PM_INPUT_BUTTON, PM_BTN_A);
    lv_obj_t* b = _gp_make_zone(btns, "B", PM_C_ERR,      PM_INPUT_BUTTON, PM_BTN_B);
    lv_obj_t* x = _gp_make_zone(btns, "X", PM_C_WARN,     PM_INPUT_BUTTON, PM_BTN_X);
    lv_obj_align(y, LV_ALIGN_TOP_MID,    0, 0);
    lv_obj_align(a, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID,  0, 0);
    lv_obj_align(x, LV_ALIGN_LEFT_MID,   0, 0);

    // Middle: Start/Select strip
    lv_obj_t* sel = _gp_make_zone(g->container, "SEL", PM_C_FG_DIM, PM_INPUT_BUTTON, PM_BTN_SELECT);
    lv_obj_t* sta = _gp_make_zone(g->container, "STA", PM_C_FG_DIM, PM_INPUT_BUTTON, PM_BTN_START);
    lv_obj_set_size(sel, 50, 30);
    lv_obj_set_size(sta, 50, 30);
    lv_obj_align(sel, LV_ALIGN_CENTER, -30, 0);
    lv_obj_align(sta, LV_ALIGN_CENTER,  30, 0);

    lv_obj_add_flag(g->container, LV_OBJ_FLAG_HIDDEN);
    return g;
}

void pm_ui_gamepad_show(pm_ui_gamepad_t* g) {
    if (g) lv_obj_clear_flag(g->container, LV_OBJ_FLAG_HIDDEN);
}
void pm_ui_gamepad_hide(pm_ui_gamepad_t* g) {
    if (g) lv_obj_add_flag(g->container, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t* pm_ui_gamepad_obj(pm_ui_gamepad_t* g) { return g ? g->container : NULL; }
