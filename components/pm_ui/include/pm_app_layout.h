// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_layout.h — Phase 16 P4 Dashboard scaffolding
//
//  Apps that want the canonical Pisces Moon dashboard layout
//  (titlebar + chips + stats row + content + action bar) can
//  build one with a handful of calls instead of ~400 lines
//  of LVGL boilerplate per app.
//
//  Pattern in an app's _build_screen():
//
//    pm_app_layout_t L = {0};
//    pm_app_layout_begin(&L, "GPS NAVIGATOR");
//    pm_app_layout_chip(&L, "FIX", PM_LAYOUT_COL_OK);
//    pm_app_layout_chip(&L, "8 SATS", PM_LAYOUT_COL_ACCENT);
//    pm_app_layout_stats_row(&L, 6);
//    pm_app_layout_stat(&L, "LAT", "37.7749");
//    pm_app_layout_stat(&L, "LON", "-122.41");
//    ...
//    pm_app_layout_content(&L);   // empty content area, fill yourself
//    pm_app_layout_action(&L, "START", PM_LAYOUT_COL_OK, _start_cb);
//    pm_app_layout_action(&L, "STOP",  PM_LAYOUT_COL_ERR, _stop_cb);
//    pm_app_layout_action(&L, "EXPORT", PM_LAYOUT_COL_ACCENT, _exp_cb);
//    pm_app_layout_end(&L);
//    // L.screen is the final lv_obj_t* — assign and load.
//
//  All sizing is automatic and adapts between 5-inch and
//  7-inch canvases based on PM_BOARD_LCD_H_RES.
// ============================================================

#ifndef PM_APP_LAYOUT_H
#define PM_APP_LAYOUT_H

#include "lvgl.h"
#include "pm_ui.h"
#include "pm_ui_p4.h"
#include "pm_board.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Common color tokens (RGB values matching pm_theme.css)
#define PM_LAYOUT_COL_ACCENT  lv_color_hex(0x4dd9ff)   // cyan — data
#define PM_LAYOUT_COL_OK      lv_color_hex(0x4dffa6)   // green — online
#define PM_LAYOUT_COL_WARN    lv_color_hex(0xffe066)   // yellow — caution
#define PM_LAYOUT_COL_ERR     lv_color_hex(0xff5577)   // red — danger
#define PM_LAYOUT_COL_GOLD    lv_color_hex(0xffd166)   // gold — nav
#define PM_LAYOUT_COL_PURPLE  lv_color_hex(0xc89eff)   // purple — creative
#define PM_LAYOUT_COL_DIM     lv_color_hex(0x5a8aa4)   // tertiary
#define PM_LAYOUT_COL_BG      lv_color_hex(0x060d14)
#define PM_LAYOUT_COL_BG2     lv_color_hex(0x0a1520)
#define PM_LAYOUT_COL_BG3     lv_color_hex(0x0f1e2c)
#define PM_LAYOUT_COL_BORDER  lv_color_hex(0x1f4060)
#define PM_LAYOUT_COL_FG      lv_color_hex(0xc8e6f5)
#define PM_LAYOUT_COL_FG_BR   lv_color_hex(0xffffff)
#define PM_LAYOUT_COL_FG_DIM  lv_color_hex(0x8db8d0)

// Layout fonts per screen size
#if PM_BOARD_LCD_H_RES <= 800
#define PM_LAYOUT_FONT_TITLE  (&lv_font_montserrat_18)
#define PM_LAYOUT_FONT_STAT   (&lv_font_montserrat_24)
#define PM_LAYOUT_FONT_LABEL  (&lv_font_montserrat_10)
#define PM_LAYOUT_FONT_TEXT   (&lv_font_montserrat_12)
#define PM_LAYOUT_FONT_BTN    (&lv_font_montserrat_12)
#define PM_LAYOUT_FONT_CHIP   (&lv_font_montserrat_10)
#define PM_LAYOUT_TITLEBAR_H  38
#define PM_LAYOUT_STATS_H     72
#define PM_LAYOUT_ACTION_H    44
#define PM_LAYOUT_ACTION_BTN_H 32
#define PM_LAYOUT_PAD         8
#else
#define PM_LAYOUT_FONT_TITLE  (&lv_font_montserrat_20)
#define PM_LAYOUT_FONT_STAT   (&lv_font_montserrat_28)
#define PM_LAYOUT_FONT_LABEL  (&lv_font_montserrat_12)
#define PM_LAYOUT_FONT_TEXT   (&lv_font_montserrat_14)
#define PM_LAYOUT_FONT_BTN    (&lv_font_montserrat_14)
#define PM_LAYOUT_FONT_CHIP   (&lv_font_montserrat_12)
#define PM_LAYOUT_TITLEBAR_H  44
#define PM_LAYOUT_STATS_H     100
#define PM_LAYOUT_ACTION_H    52
#define PM_LAYOUT_ACTION_BTN_H 36
#define PM_LAYOUT_PAD         12
#endif

// Layout context — apps allocate one on the stack
typedef struct {
    lv_obj_t* screen;        // root screen, returned to app
    lv_obj_t* titlebar;      // top bar
    lv_obj_t* title_spacer;  // grows so chips push right
    lv_obj_t* stats_row;     // optional stats grid
    lv_obj_t* content;       // main content area (flex column)
    lv_obj_t* action_bar;    // bottom button strip
    int       stats_cells;   // configured cell count
    int       stats_added;
} pm_app_layout_t;

// Lifecycle
void pm_app_layout_begin(pm_app_layout_t* L, const char* title);
lv_obj_t* pm_app_layout_end(pm_app_layout_t* L);

// Titlebar chips (after begin, before stats/content)
//   Returns the chip's label widget so apps can update text later
lv_obj_t* pm_app_layout_chip(pm_app_layout_t* L,
                              const char* text, lv_color_t color);

// Optional stats row — call once with cell count, then call stat() N times.
//   Returns the value label so apps can update numbers live.
void pm_app_layout_stats_row(pm_app_layout_t* L, int cells);
lv_obj_t* pm_app_layout_stat(pm_app_layout_t* L,
                              const char* label, const char* initial_value);

// Begin the main content panel area. Apps populate by adding
// children to L->content. The content is a flex container with
// row direction by default; apps can switch by calling
// lv_obj_set_flex_flow(L->content, ...).
void pm_app_layout_content(pm_app_layout_t* L);

// Make a left/right pane inside content — common 2/3-column layout.
//   Apps call pane(L, width) to get a panel; subsequent panes flow next.
//   width=0 means "flex grow to fill remaining space".
lv_obj_t* pm_app_layout_pane(pm_app_layout_t* L, int width_px,
                               const char* header_text);

// Action bar — call as many as fit, ~3-5 typical.
//   Sets a uniform button height and consistent style.
lv_obj_t* pm_app_layout_action(pm_app_layout_t* L,
                                 const char* label, lv_color_t color,
                                 lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_LAYOUT_H
