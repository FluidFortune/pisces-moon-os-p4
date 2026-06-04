// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_cyber.c — Cyberpunk Circuit Aesthetic implementation
//
//  Pure LVGL widgets, no canvas. Static at build time.
// ============================================================

#include "pm_cyber.h"
#include "pm_board.h"
#include <string.h>

// ── Spectrum table ────────────────────────────────────────
// Eight-color spectrum matching S3 splash:
//   red, amber, yellow, green, cyan, blue, magenta, white
const lv_color_t pm_cyber_spectrum[8] = {
    LV_COLOR_MAKE(0xFF, 0x00, 0x00),  // red
    LV_COLOR_MAKE(0xFF, 0xA8, 0x00),  // amber
    LV_COLOR_MAKE(0xFF, 0xFF, 0x00),  // yellow
    LV_COLOR_MAKE(0x00, 0xFF, 0x00),  // green
    LV_COLOR_MAKE(0x00, 0xFF, 0xFF),  // cyan
    LV_COLOR_MAKE(0x00, 0x00, 0xFF),  // blue
    LV_COLOR_MAKE(0xFF, 0x00, 0xFF),  // magenta
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  // white
};

// ─────────────────────────────────────────────────────────
//  PCB Background
//
//  Layered widgets:
//    1. Parent gets matte black fill
//    2. Dim grid via repeated 1px vertical+horizontal lines
//    3. Trace runs (right-angle jogs)
//    4. Solder pads at junctions
//
//  All are children of parent. We use lv_obj's for lines
//  (set border 1px on one side) which is the cheapest path
//  in LVGL 9.
// ─────────────────────────────────────────────────────────

static void _vline(lv_obj_t* parent, int x, int y, int h, lv_color_t color) {
    lv_obj_t* l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, 1, h);
    lv_obj_set_style_bg_color(l, color, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void _hline(lv_obj_t* parent, int x, int y, int w, lv_color_t color) {
    lv_obj_t* l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_style_bg_color(l, color, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void _pad(lv_obj_t* parent, int x, int y, int sz, lv_color_t color) {
    lv_obj_t* l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, sz, sz);
    lv_obj_set_style_bg_color(l, color, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

// Draw a diagonal line from (x1,y1) to (x2,y2) as a thin lv_line.
// This is more expensive than the rect-based v/hlines, so use
// sparingly (chamfer corners only).
//
// Use lv_line_set_points (copying variant) so each line owns its
// own geometry. The mutable variant keeps the caller's pointer —
// fine for a single line, but a foot-gun when the helper is called
// repeatedly with a static local buffer (every line would track the
// last call's coordinates).
static void _diag(lv_obj_t* parent, int x1, int y1, int x2, int y2,
                   lv_color_t color, int thickness) {
    lv_point_precise_t pts[2];
    pts[0].x = x1; pts[0].y = y1;
    pts[1].x = x2; pts[1].y = y2;
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, thickness, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

void pm_cyber_draw_pcb_bg(lv_obj_t* parent, int w, int h) {
    if (!parent) return;

    lv_obj_set_style_bg_color(parent, PM_CYBER_C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // Grid spacing — 24px on 1024×600, 20px on 800×480.
    const int step = (w >= 900) ? 24 : 20;

    // Vertical grid lines
    for (int x = step; x < w; x += step) {
        _vline(parent, x, 0, h, PM_CYBER_C_GRID);
    }
    // Horizontal grid lines
    for (int y = step; y < h; y += step) {
        _hline(parent, 0, y, w, PM_CYBER_C_GRID);
    }

    // PCB traces — sprinkled around the edges with right-angle jogs.
    // Layout sized for 1024×600 but scales reasonably to 800×480.
    int margin = 32;
    int qw = w / 4;
    int qh = h / 4;

    // Top-left trace cluster
    _hline(parent, 0, margin, qw - 60, PM_CYBER_C_TRACE);
    _vline(parent, qw - 60, margin, 30, PM_CYBER_C_TRACE);
    _hline(parent, qw - 60, margin + 30, 90, PM_CYBER_C_TRACE);
    _pad  (parent, qw - 62, margin - 2, 5, PM_CYBER_C_PAD);

    // Top-right
    _hline(parent, w - qw, margin, qw, PM_CYBER_C_TRACE);
    _vline(parent, w - qw, margin, 20, PM_CYBER_C_TRACE);
    _hline(parent, w - qw - 40, margin + 20, 40, PM_CYBER_C_TRACE);
    _pad  (parent, w - qw - 42, margin + 18, 5, PM_CYBER_C_PAD);

    // Mid-left
    _hline(parent, 0, qh + 20, qw - 100, PM_CYBER_C_TRACE);
    _vline(parent, qw - 100, qh + 20, 20, PM_CYBER_C_TRACE);
    _pad  (parent, qw - 102, qh + 18, 5, PM_CYBER_C_PAD);

    // Mid-right
    _hline(parent, w - (qw - 100), qh + 20, qw - 100, PM_CYBER_C_TRACE);
    _vline(parent, w - (qw - 100), qh + 20, 20, PM_CYBER_C_TRACE);
    _pad  (parent, w - (qw - 100) - 2, qh + 18, 5, PM_CYBER_C_PAD);

    // Bottom-left
    _hline(parent, 0, h - margin - qh / 2, qw / 2, PM_CYBER_C_TRACE);
    _vline(parent, qw / 2, h - margin - qh / 2, qh / 2, PM_CYBER_C_TRACE);
    _hline(parent, qw / 2, h - margin, qw / 2, PM_CYBER_C_TRACE);
    _pad  (parent, qw / 2 - 2, h - margin - 2, 5, PM_CYBER_C_PAD);

    // Bottom-right
    _hline(parent, w - qw, h - margin, qw, PM_CYBER_C_TRACE);
    _vline(parent, w - qw, h - margin - 30, 30, PM_CYBER_C_TRACE);
    _hline(parent, w - qw - 50, h - margin - 30, 50, PM_CYBER_C_TRACE);
    _pad  (parent, w - qw - 52, h - margin - 32, 5, PM_CYBER_C_PAD);

    // Diagonal accents (corner suggestions)
    _diag(parent, w - 60, 8, w - 4, 64, PM_CYBER_C_TRACE, 1);
    _diag(parent, 4, h - 64, 60, h - 8, PM_CYBER_C_TRACE, 1);
}

// ─────────────────────────────────────────────────────────
//  Chamfered Tile
//
//  Implementation: a single lv_button styled as a rectangle
//  + 4 short diagonal lines at the corners that mask the
//  hard 90° corners with chamfers.
//
//  We use 8px corner cuts (matches S3 spec).
// ─────────────────────────────────────────────────────────

#define PM_CYBER_TILE_CUT  8

lv_obj_t* pm_cyber_make_tile(lv_obj_t* parent, int w, int h,
                              lv_color_t accent, bool selected) {
    lv_obj_t* tile = lv_button_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, w, h);

    lv_color_t fill = selected ? PM_CYBER_C_SEL_FILL : PM_CYBER_C_DARK;
    lv_obj_set_style_bg_color(tile, fill, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    // Faux chamfer via radius (LVGL 9 doesn't do true polygon clipping
    // cheaply, so we settle for a tight rounded corner that visually
    // reads as a chamfer from arm's length).
    lv_obj_set_style_radius(tile, PM_CYBER_TILE_CUT, 0);

    // Border in accent color, brighter when selected
    lv_obj_set_style_border_color(tile, accent, 0);
    lv_obj_set_style_border_width(tile, selected ? 3 : 2, 0);
    lv_obj_set_style_border_opa(tile, selected ? LV_OPA_COVER : 220, 0);

    // Subtle inner shadow when selected
    if (selected) {
        lv_obj_set_style_shadow_color(tile, accent, 0);
        lv_obj_set_style_shadow_opa(tile, 80, 0);
        lv_obj_set_style_shadow_width(tile, 8, 0);
        lv_obj_set_style_shadow_spread(tile, 1, 0);
    }

    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    return tile;
}

void pm_cyber_tile_set_selected(lv_obj_t* tile, lv_color_t accent,
                                 bool selected) {
    if (!tile) return;
    lv_color_t fill = selected ? PM_CYBER_C_SEL_FILL : PM_CYBER_C_DARK;
    lv_obj_set_style_bg_color(tile, fill, 0);
    lv_obj_set_style_border_color(tile, accent, 0);
    lv_obj_set_style_border_width(tile, selected ? 3 : 2, 0);
    lv_obj_set_style_border_opa(tile, selected ? LV_OPA_COVER : 220, 0);
    if (selected) {
        lv_obj_set_style_shadow_color(tile, accent, 0);
        lv_obj_set_style_shadow_opa(tile, 80, 0);
        lv_obj_set_style_shadow_width(tile, 8, 0);
        lv_obj_set_style_shadow_spread(tile, 1, 0);
    } else {
        lv_obj_set_style_shadow_opa(tile, 0, 0);
    }
}

// ─────────────────────────────────────────────────────────
//  Icon trace stubs
//
//  Short lines + tiny pads running off the tile edges, giving
//  each tile the "wired into the circuit" look.
// ─────────────────────────────────────────────────────────
void pm_cyber_draw_tile_traces(lv_obj_t* parent, int tx, int ty,
                                int tw, int th, lv_color_t color) {
    if (!parent) return;
    int mx = tx + tw / 2;
    int my = ty + th / 2;

    // Top trace stub
    _vline(parent, mx - 8, ty - 8, 8, color);
    _hline(parent, mx - 16, ty - 8, 8, color);
    _pad  (parent, mx - 18, ty - 10, 4, color);

    // Bottom trace stub
    _vline(parent, mx + 8, ty + th, 8, color);
    _hline(parent, mx + 4, ty + th + 8, 10, color);
    _pad  (parent, mx + 14, ty + th + 6, 4, color);

    // Right trace stub
    _hline(parent, tx + tw, my - 6, 8, color);
    _vline(parent, tx + tw + 8, my - 14, 10, color);
    _pad  (parent, tx + tw + 6, my - 16, 4, color);

    // Left trace stub (shorter)
    _hline(parent, tx - 8, my + 4, 8, color);
    _pad  (parent, tx - 10, my + 2, 4, color);
}

// ─────────────────────────────────────────────────────────
//  Chip icon — DIP package with colored pin tips
//
//  Renders as a compound widget: a dark body rect, bright
//  green outline, inner detail lines, and short pin stubs
//  along the top and bottom edges with rainbow tips.
// ─────────────────────────────────────────────────────────
void pm_cyber_draw_chip_icon(lv_obj_t* parent, int cx, int cy, int scale) {
    if (!parent) return;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    int body_w = 28 * scale;
    int body_h = 18 * scale;
    int x = cx - body_w / 2;
    int y = cy - body_h / 2;

    // Body
    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, body_w, body_h);
    lv_obj_set_style_bg_color(body, PM_CYBER_C_CHAMFER, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(body, PM_CYBER_C_MATRIX, 0);
    lv_obj_set_style_border_width(body, 1 + scale / 2, 0);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Inner detail lines (the "die" marks)
    int detail_w = body_w - 8 * scale;
    int detail_x = x + 4 * scale;
    for (int i = 0; i < 3; i++) {
        int dy = y + body_h / 4 + (i * (body_h / 4));
        _hline(parent, detail_x, dy, detail_w, PM_CYBER_C_TRACE);
    }

    // Pin stubs on left side with rainbow tips
    int pin_count = 4;
    int pin_spacing = body_h / (pin_count + 1);
    int pin_len = 4 * scale;
    for (int i = 0; i < pin_count; i++) {
        int py = y + pin_spacing * (i + 1);
        _hline(parent, x - pin_len, py, pin_len, PM_CYBER_C_MATRIX);
        // Tip pad (colored)
        lv_color_t tip = pm_cyber_spectrum[i % 8];
        _pad(parent, x - pin_len - 2, py - 1, 3, tip);
    }
    // Pin stubs on right side
    for (int i = 0; i < pin_count; i++) {
        int py = y + pin_spacing * (i + 1);
        _hline(parent, x + body_w, py, pin_len, PM_CYBER_C_MATRIX);
        lv_color_t tip = pm_cyber_spectrum[(7 - i) % 8];
        _pad(parent, x + body_w + pin_len, py - 1, 3, tip);
    }
}

// ─────────────────────────────────────────────────────────
//  Rainbow per-character label
//
//  Implementation: a flex-row container with one label per
//  character. Each label's color comes from pm_cyber_spectrum
//  with a per-call offset. Cycling animates by calling
//  pm_cyber_rainbow_label_cycle() repeatedly.
//
//  Returns the first character label so the caller can walk
//  siblings to recolor. The container itself is the first
//  label's parent.
// ─────────────────────────────────────────────────────────

lv_obj_t* pm_cyber_rainbow_label(lv_obj_t* parent, const char* text,
                                  const lv_font_t* font,
                                  int cycle_offset) {
    if (!parent || !text) return NULL;

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* first = NULL;
    int len = (int)strlen(text);
    for (int i = 0; i < len; i++) {
        char ch[2] = { text[i], 0 };
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, ch);
        if (font) lv_obj_set_style_text_font(lbl, font, 0);
        lv_color_t col = pm_cyber_spectrum[(i + cycle_offset) & 7];
        lv_obj_set_style_text_color(lbl, col, 0);
        if (!first) first = lbl;
    }
    return first;
}

void pm_cyber_rainbow_label_cycle(lv_obj_t* first_char_lbl,
                                   int char_count, int cycle_offset) {
    if (!first_char_lbl) return;
    lv_obj_t* parent = lv_obj_get_parent(first_char_lbl);
    if (!parent) return;
    for (int i = 0; i < char_count; i++) {
        lv_obj_t* lbl = lv_obj_get_child(parent, i);
        if (!lbl) break;
        lv_color_t col = pm_cyber_spectrum[(i + cycle_offset) & 7];
        lv_obj_set_style_text_color(lbl, col, 0);
    }
}
