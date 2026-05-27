// ============================================================
//  pm_ui_p4.h — Pisces Moon P4 Layout & Color Constants
//  Copyright (C) 2026 Eric Becker / Fluid Fortune
//  SPDX-License-Identifier: AGPL-3.0-or-later
//
//  Add this to pm_ui.h or #include it from pm_ui.h.
//  These constants are derived from the web app design language
//  (pm_theme.css / webapptest.html) translated to LVGL/P4.
// ============================================================

#pragma once
#include "lvgl.h"
#include "pm_board.h"

// ── Display geometry ──────────────────────────────────────────
#define PM_SCREEN_W         PM_BOARD_LCD_H_RES
#define PM_SCREEN_H         PM_BOARD_LCD_V_RES

// ── Structural heights ────────────────────────────────────────
// Status bar: persistent, overlays every screen
#define PM_STATUS_BAR_H     32
// App titlebar (below status bar, per-app)
#define PM_TITLEBAR_H       44
// Available content area below both bars
#define PM_CONTENT_H        (PM_SCREEN_H - PM_STATUS_BAR_H - PM_TITLEBAR_H)
// Bottom action bar height (when present)
#define PM_ACTION_BAR_H     52
// Thin game header/footer
#define PM_GAME_STRIP_H     38

// ── Common panel widths ───────────────────────────────────────
#define PM_SIDEBAR_W        (PM_SCREEN_W >= 1000 ? 280 : 220)
#define PM_LOG_PANEL_W      (PM_SCREEN_W >= 1000 ? 340 : 260)
#define PM_CHAT_SIDEBAR_W   (PM_SCREEN_W >= 1000 ? 260 : 220)
#define PM_LIBRARY_W        (PM_SCREEN_W >= 1000 ? 300 : 240)

// ── Launcher tile dimensions ──────────────────────────────────
// 7 tiles, flex-wrap, 3 across on 1024px with 12px gaps
// Row 1: 3 tiles @ 280px + 2×12px gap = 864px (centered in 1024px)
// Row 2: 4 tiles @ 220px + 3×12px gap = 916px
// Simplest: uniform 280×140 all 7, flex-wrap, center-aligned
#define PM_TILE_W           (PM_SCREEN_W >= 1000 ? 280 : 220)
#define PM_TILE_H           (PM_SCREEN_H >= 600 ? 140 : 112)
#define PM_TILE_GAP         (PM_SCREEN_W >= 1000 ? 14 : 10)

// ── Stat block (dashboards — wardrive, scanner, etc.) ─────────
// 4 equal blocks across full width
#define PM_STAT_BLOCK_H     (PM_SCREEN_H >= 600 ? 100 : 78)
#define PM_STAT_NUM_FONT    (&lv_font_montserrat_28)
#define PM_STAT_LABEL_FONT  (&lv_font_montserrat_10)

// ── Card heights ──────────────────────────────────────────────
#define PM_CARD_H_SM        56
#define PM_CARD_H_MD        80
#define PM_CARD_H_LG        120

// ── Padding & gaps ────────────────────────────────────────────
#define PM_PAD_XS           4
#define PM_PAD_SM           8
#define PM_PAD_MD           12
#define PM_PAD_LG           16
#define PM_PAD_XL           24

// Colors defined in pm_ui.h — do not redefine here
// Category colors (new additions not in pm_ui.h):
#define PM_C_CAT_COMMS      lv_color_hex(0x00d4ff)
#define PM_C_CAT_CYBER      lv_color_hex(0xff3366)
#define PM_C_CAT_TOOLS      lv_color_hex(0xf4a820)
#define PM_C_CAT_INTEL      lv_color_hex(0x00ff88)
#define PM_C_CAT_MEDIA      lv_color_hex(0xff6b00)
#define PM_C_CAT_GAMES      lv_color_hex(0xa855f7)
#define PM_C_CAT_SYSTEM     lv_color_hex(0x4a7a92)
