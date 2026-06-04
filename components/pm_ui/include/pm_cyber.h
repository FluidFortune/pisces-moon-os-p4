// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_cyber.h — Cyberpunk Circuit Aesthetic primitives
//
//  Shared widgets for the boot splash, launcher, and any app
//  that wants the PCB-board look. Derived from the S3
//  launcher.cpp / main.cpp showRainbowSplash reference.
//
//  Visual identity:
//    - Matrix green primary (0x07E0 / #00FF00)
//    - Dim green PCB grid background (20-24px spacing)
//    - PCB trace runs with right-angle jogs
//    - Solder pad squares at junctions
//    - Chamfered (hexagonal) tiles with category-color borders
//    - Icon-trace stubs running off tile edges
//    - Rainbow per-character title cycling (splash only)
//
//  Token-sipping: pure LVGL widgets (no canvas, no bitmaps).
//  All vector. Static at build time, no per-frame redraw.
// ============================================================

#ifndef PM_CYBER_H
#define PM_CYBER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Cyberpunk Palette ──────────────────────────────────────
// Mirror of S3 launcher.cpp THEME block, ported to RGB888.
//
//   C_MATRIX   — bright green primary (icons, text, accents)
//   C_DARK     — near-black with blue tint (tile fills)
//   C_GRID     — very dim green (grid lines)
//   C_TRACE    — slightly brighter green (PCB traces)
//   C_PAD      — solder pad color
//   C_CHAMFER  — mid-green chamfer line
//   C_GLOW     — icon glow / selected highlight
//   C_HEADER   — deep blue-black header strip
#define PM_CYBER_C_BLACK    lv_color_hex(0x000000)
#define PM_CYBER_C_DARK     lv_color_hex(0x000810)
#define PM_CYBER_C_BG       lv_color_hex(0x000408)
#define PM_CYBER_C_HEADER   lv_color_hex(0x000810)
#define PM_CYBER_C_GRID     lv_color_hex(0x002408)
#define PM_CYBER_C_TRACE    lv_color_hex(0x006800)
#define PM_CYBER_C_PAD      lv_color_hex(0x00AC00)
#define PM_CYBER_C_CHAMFER  lv_color_hex(0x00C000)
#define PM_CYBER_C_MATRIX   lv_color_hex(0x00FF00)
#define PM_CYBER_C_GLOW     lv_color_hex(0x80FF40)
#define PM_CYBER_C_GREY     lv_color_hex(0x7B7B7B)
#define PM_CYBER_C_WHITE    lv_color_hex(0xFFFFFF)
#define PM_CYBER_C_SEL_FILL lv_color_hex(0x000820)

// Category accent colors — drives tile chamfer + icon color.
// Mirror of CAT_ACCENT[] in S3 launcher.cpp.
#define PM_CYBER_CAT_COMMS  lv_color_hex(0x00BFCF)
#define PM_CYBER_CAT_CYBER  lv_color_hex(0xFF6000)
#define PM_CYBER_CAT_TOOLS  lv_color_hex(0xFFA800)
#define PM_CYBER_CAT_GAMES  lv_color_hex(0x00FF00)
#define PM_CYBER_CAT_INTEL  lv_color_hex(0x00FFFF)
#define PM_CYBER_CAT_MEDIA  lv_color_hex(0xFF00FF)
#define PM_CYBER_CAT_SYSTEM lv_color_hex(0xB0B0B0)

// Rainbow spectrum for per-char title cycling (8 colors).
extern const lv_color_t pm_cyber_spectrum[8];

// ── PCB Background ─────────────────────────────────────────
//
// Fills parent with: black bg, dim green grid, scattered PCB
// trace runs with right-angle jogs, and solder pads at trace
// junctions. Sized to the parent's content area.
//
// Call once at screen-build time. The widgets it creates are
// children of `parent` and live for the lifetime of `parent`.
// No per-frame work.
void pm_cyber_draw_pcb_bg(lv_obj_t* parent, int w, int h);

// ── Chamfered Tile ─────────────────────────────────────────
//
// Creates a hexagonal-cornered tile widget. The tile is a
// clickable lv_button so apps can attach LV_EVENT_CLICKED.
//
//   parent       — container
//   w, h         — tile size (square works best, 160-200 px)
//   accent       — chamfer border color (use a PM_CYBER_CAT_* token)
//   selected     — true → lighter fill + brighter border
//
// Layout inside: tile uses flex-column; add label children
// directly with lv_label_create(tile).
lv_obj_t* pm_cyber_make_tile(lv_obj_t* parent, int w, int h,
                              lv_color_t accent, bool selected);

// Update an existing tile's selected state (recolors border + fill).
void pm_cyber_tile_set_selected(lv_obj_t* tile, lv_color_t accent,
                                 bool selected);

// ── Icon trace stubs ───────────────────────────────────────
//
// Draws short PCB trace stubs running off the edges of a tile,
// making it look "wired into the circuit". Call after creating
// the tile, passing the tile's parent and the tile's geometry.
//
// Token-sip: 4 lines + 2 small rects per tile.
void pm_cyber_draw_tile_traces(lv_obj_t* parent, int tile_x, int tile_y,
                                int tile_w, int tile_h, lv_color_t color);

// ── Chip icon (DIP package) ────────────────────────────────
//
// Renders an ESP32-style DIP chip body with colored pin tips.
// Centered at (cx, cy) in `parent`. Scale: 1 = small (28x20),
// 2 = medium (56x40), 3 = large splash (84x60).
//
// Used by the splash and optionally as a category-tile icon.
void pm_cyber_draw_chip_icon(lv_obj_t* parent, int cx, int cy, int scale);

// ── Rainbow title label ────────────────────────────────────
//
// Creates a label per character, each colored from the
// spectrum starting at `cycle_offset`. Returns the first
// character's label (rest are siblings with sequential indices)
// for animation callers.
//
// Used by the boot splash for the "Pisces Moon" title.
// `text` is copied; layout is horizontal flex-row.
lv_obj_t* pm_cyber_rainbow_label(lv_obj_t* parent, const char* text,
                                  const lv_font_t* font,
                                  int cycle_offset);

// Re-recolor an existing rainbow label group. `first_char_lbl`
// must be the return value from pm_cyber_rainbow_label().
void pm_cyber_rainbow_label_cycle(lv_obj_t* first_char_lbl,
                                   int char_count, int cycle_offset);

#ifdef __cplusplus
}
#endif

#endif  // PM_CYBER_H
