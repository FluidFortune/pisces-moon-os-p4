// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_layout.c — Phase 16 P4 Dashboard layout helper
// ============================================================

#include "pm_app_layout.h"
#include "pm_hal.h"
#include "pm_launcher.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_LAYOUT";

static void _back_cb(lv_event_t* e) {
    (void)e;
    pm_launcher_back_from_app();
}

void pm_app_layout_begin(pm_app_layout_t* L, const char* title) {
    if (!L) return;
    memset(L, 0, sizeof(*L));

    // Root screen
    L->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(L->screen, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(L->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(L->screen, 0, 0);
    lv_obj_set_style_pad_gap(L->screen, 0, 0);
    lv_obj_set_layout(L->screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(L->screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(L->screen, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(L->screen, LV_OBJ_FLAG_SCROLLABLE);

    // Titlebar
    L->titlebar = lv_obj_create(L->screen);
    lv_obj_remove_style_all(L->titlebar);
    lv_obj_set_width(L->titlebar, LV_PCT(100));
    lv_obj_set_height(L->titlebar, PM_LAYOUT_TITLEBAR_H);
    lv_obj_set_style_bg_color(L->titlebar, PM_LAYOUT_COL_BG2, 0);
    lv_obj_set_style_bg_opa(L->titlebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(L->titlebar, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(L->titlebar, 1, 0);
    lv_obj_set_style_border_side(L->titlebar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(L->titlebar, PM_LAYOUT_PAD, 0);
    lv_obj_set_style_pad_ver(L->titlebar, 0, 0);
    lv_obj_set_style_pad_column(L->titlebar, 8, 0);
    lv_obj_set_layout(L->titlebar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(L->titlebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(L->titlebar, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(L->titlebar, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t* back = lv_btn_create(L->titlebar);
    lv_obj_remove_style_all(back);
    int sz = PM_LAYOUT_TITLEBAR_H - 12;
    lv_obj_set_size(back, sz, sz);
    lv_obj_set_style_bg_color(back, PM_LAYOUT_COL_BG3, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, 4, 0);
    lv_obj_set_style_bg_opa(back, 60, LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, _back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t* tlbl = lv_label_create(L->titlebar);
    lv_label_set_text(tlbl, title ? title : "APP");
    lv_obj_set_style_text_font(tlbl, PM_LAYOUT_FONT_TITLE, 0);
    lv_obj_set_style_text_color(tlbl, PM_LAYOUT_COL_FG_BR, 0);
    lv_obj_set_style_text_letter_space(tlbl, 1, 0);

    // Spacer — pushes any subsequent chips to the right
    L->title_spacer = lv_obj_create(L->titlebar);
    lv_obj_remove_style_all(L->title_spacer);
    lv_obj_set_height(L->title_spacer, 1);
    lv_obj_set_flex_grow(L->title_spacer, 1);

    pm_log_d(TAG, "begin '%s'", title ? title : "");
}

lv_obj_t* pm_app_layout_chip(pm_app_layout_t* L,
                              const char* text, lv_color_t color) {
    if (!L || !L->titlebar) return NULL;
    lv_obj_t* chip = lv_obj_create(L->titlebar);
    lv_obj_remove_style_all(chip);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_set_style_bg_opa(chip, 30, 0);
    lv_obj_set_style_border_color(chip, color, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 3, 0);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(chip);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_font(lbl, PM_LAYOUT_FONT_CHIP, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);
    return lbl;
}

void pm_app_layout_stats_row(pm_app_layout_t* L, int cells) {
    if (!L || !L->screen) return;
    if (cells <= 0) cells = 4;
    if (cells > 8) cells = 8;
    L->stats_cells = cells;
    L->stats_added = 0;

    L->stats_row = lv_obj_create(L->screen);
    lv_obj_remove_style_all(L->stats_row);
    lv_obj_set_width(L->stats_row, LV_PCT(100));
    lv_obj_set_height(L->stats_row, PM_LAYOUT_STATS_H);
    lv_obj_set_style_bg_color(L->stats_row, PM_LAYOUT_COL_BG2, 0);
    lv_obj_set_style_bg_opa(L->stats_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(L->stats_row, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(L->stats_row, 1, 0);
    lv_obj_set_style_border_side(L->stats_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_layout(L->stats_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(L->stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(L->stats_row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(L->stats_row, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* pm_app_layout_stat(pm_app_layout_t* L,
                               const char* label, const char* initial) {
    if (!L || !L->stats_row) return NULL;
    if (L->stats_added >= L->stats_cells) return NULL;

    lv_obj_t* cell = lv_obj_create(L->stats_row);
    lv_obj_remove_style_all(cell);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_height(cell, LV_PCT(100));
    lv_obj_set_style_bg_color(cell, PM_LAYOUT_COL_BG2, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    bool last = (L->stats_added == L->stats_cells - 1);
    if (!last) {
        lv_obj_set_style_border_color(cell, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT, 0);
    }
    lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cell, 6, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* val = lv_label_create(cell);
    lv_label_set_text(val, initial ? initial : "—");
    lv_obj_set_style_text_font(val, PM_LAYOUT_FONT_STAT, 0);
    lv_obj_set_style_text_color(val, PM_LAYOUT_COL_FG_BR, 0);

    lv_obj_t* lbl = lv_label_create(cell);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_font(lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    L->stats_added++;
    return val;
}

void pm_app_layout_content(pm_app_layout_t* L) {
    if (!L || !L->screen) return;
    if (L->content) return;

    L->content = lv_obj_create(L->screen);
    lv_obj_remove_style_all(L->content);
    lv_obj_set_width(L->content, LV_PCT(100));
    lv_obj_set_flex_grow(L->content, 1);
    lv_obj_set_style_bg_color(L->content, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(L->content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(L->content, 0, 0);
    lv_obj_set_style_pad_gap(L->content, 0, 0);
    lv_obj_set_layout(L->content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(L->content, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(L->content, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* pm_app_layout_pane(pm_app_layout_t* L, int width_px,
                               const char* header_text) {
    if (!L) return NULL;
    if (!L->content) pm_app_layout_content(L);

    lv_obj_t* pane = lv_obj_create(L->content);
    lv_obj_remove_style_all(pane);
    if (width_px <= 0) {
        lv_obj_set_flex_grow(pane, 1);
    } else {
        lv_obj_set_width(pane, width_px);
    }
    lv_obj_set_height(pane, LV_PCT(100));
    lv_obj_set_style_bg_color(pane, PM_LAYOUT_COL_BG2, 0);
    lv_obj_set_style_bg_opa(pane, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pane, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(pane, 1, 0);
    lv_obj_set_style_border_side(pane, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_layout(pane, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(pane, 0, 0);
    lv_obj_clear_flag(pane, LV_OBJ_FLAG_SCROLLABLE);

    if (header_text) {
        lv_obj_t* hdr = lv_obj_create(pane);
        lv_obj_remove_style_all(hdr);
        lv_obj_set_width(hdr, LV_PCT(100));
        lv_obj_set_height(hdr, 28);
        lv_obj_set_style_bg_color(hdr, PM_LAYOUT_COL_BG3, 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(hdr, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(hdr, 1, 0);
        lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_hor(hdr, 10, 0);
        lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* hlbl = lv_label_create(hdr);
        lv_label_set_text(hlbl, header_text);
        lv_obj_set_style_text_font(hlbl, PM_LAYOUT_FONT_LABEL, 0);
        lv_obj_set_style_text_color(hlbl, PM_LAYOUT_COL_DIM, 0);
        lv_obj_set_style_text_letter_space(hlbl, 1, 0);
        lv_obj_align(hlbl, LV_ALIGN_LEFT_MID, 0, 0);
    }
    return pane;
}

lv_obj_t* pm_app_layout_action(pm_app_layout_t* L,
                                 const char* label, lv_color_t color,
                                 lv_event_cb_t cb) {
    if (!L || !L->screen) return NULL;
    if (!L->action_bar) {
        L->action_bar = lv_obj_create(L->screen);
        lv_obj_remove_style_all(L->action_bar);
        lv_obj_set_width(L->action_bar, LV_PCT(100));
        lv_obj_set_height(L->action_bar, PM_LAYOUT_ACTION_H);
        lv_obj_set_style_bg_color(L->action_bar, PM_LAYOUT_COL_BG2, 0);
        lv_obj_set_style_bg_opa(L->action_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(L->action_bar, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(L->action_bar, 1, 0);
        lv_obj_set_style_border_side(L->action_bar, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_pad_hor(L->action_bar, PM_LAYOUT_PAD, 0);
        lv_obj_set_style_pad_ver(L->action_bar, 0, 0);
        lv_obj_set_style_pad_column(L->action_bar, 8, 0);
        lv_obj_set_layout(L->action_bar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(L->action_bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(L->action_bar, LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(L->action_bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* btn = lv_btn_create(L->action_bar);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, 25, 0);
    lv_obj_set_style_bg_opa(btn, 80, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    lv_obj_set_height(btn, PM_LAYOUT_ACTION_BTN_H);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_font(lbl, PM_LAYOUT_FONT_BTN, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t* pm_app_layout_end(pm_app_layout_t* L) {
    if (!L) return NULL;
    // Ensure content exists even if app didn't call it
    if (!L->content) pm_app_layout_content(L);
    return L->screen;
}
