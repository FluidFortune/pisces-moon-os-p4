// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_ui.h — Pisces UI Kit
//
//  Small palette of LVGL widget builders shared by all 49
//  apps so the OS looks coherent and individual app screens
//  stay short. Each app's _build_screen() typically calls
//  three or four of these helpers and is done.
//
//  Provides:
//    - Color/theme tokens (Pisces Moon palette)
//    - pm_ui_screen()        — fresh screen with our base style
//    - pm_ui_titlebar()      — top bar with app name + back btn
//    - pm_ui_card()          — bordered container
//    - pm_ui_button()        — themed button
//    - pm_ui_chip()          — small status pill
//    - pm_ui_kv_row()        — "key: value" row, returns value label
//    - pm_ui_status_dot()    — colored circle for state
//    - pm_ui_list()          — themed scrollable list
//    - pm_ui_meter_bar()     — horizontal meter (RSSI, level)
//    - pm_ui_keypad()        — calc-style numeric pad
//    - pm_ui_log_panel()     — append-only scrollback
//    - pm_ui_grid()          — quick column layout
//
//  All helpers return lv_obj_t* — apps store handles for
//  later updates. The screen's _render() is just a series of
//  lv_label_set_text() / lv_bar_set_value() / etc. calls.
//
//  This kit is the difference between 49 grim ad-hoc UIs and
//  a coherent product.
// ============================================================

#ifndef PM_UI_H
#define PM_UI_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Palette ─────────────────────────────────────────────────
// Pisces Moon brand: deep blue/teal sea, lunar white, accents.
#define PM_C_BG          lv_color_hex(0x0A1828)   // deep sea
#define PM_C_BG_2        lv_color_hex(0x122B45)   // card bg
#define PM_C_BG_3        lv_color_hex(0x1A3A5C)   // raised
#define PM_C_FG          lv_color_hex(0xE6F0FA)   // moonlight
#define PM_C_FG_DIM      lv_color_hex(0x8FA8C2)
#define PM_C_ACCENT      lv_color_hex(0x4FD1C5)   // teal moon
#define PM_C_ACCENT_2    lv_color_hex(0xB4A0FF)   // pisces purple
#define PM_C_OK          lv_color_hex(0x4ADE80)
#define PM_C_WARN        lv_color_hex(0xFBBF24)
#define PM_C_ERR         lv_color_hex(0xF87171)
#define PM_C_BORDER      lv_color_hex(0x2A4A6C)

// ── Theme init ──────────────────────────────────────────────
// Call once after pm_bsp_init(). Sets default fonts and theme
// colors on the active display.
void pm_ui_theme_init(void);

// ── Builders ────────────────────────────────────────────────

// Fresh screen with the base background. Use this instead of
// lv_obj_create(NULL) so styling is uniform.
lv_obj_t* pm_ui_screen(void);

// Titlebar at top: title text, optional back-button event_cb.
// Returns the bar so apps can add right-aligned controls.
lv_obj_t* pm_ui_titlebar(lv_obj_t* parent, const char* title,
                          lv_event_cb_t back_cb, void* back_user);

// Card container (rounded, bordered, padded).
lv_obj_t* pm_ui_card(lv_obj_t* parent);

// Button with a label. event_cb fires on LV_EVENT_CLICKED.
lv_obj_t* pm_ui_button(lv_obj_t* parent, const char* label,
                        lv_event_cb_t cb, void* user);

// Chip / pill for status text.
lv_obj_t* pm_ui_chip(lv_obj_t* parent, const char* text, lv_color_t color);

// Returns the *value* label so apps can update it later
// with lv_label_set_text(value_lbl, ...).
lv_obj_t* pm_ui_kv_row(lv_obj_t* parent, const char* key, const char* initial);

// Colored dot for boolean status. Returns the dot for later
// recoloring with lv_obj_set_style_bg_color().
lv_obj_t* pm_ui_status_dot(lv_obj_t* parent, lv_color_t color);

// Themed scrollable list — set its height with lv_obj_set_height.
lv_obj_t* pm_ui_list(lv_obj_t* parent);

// Horizontal bar meter. Use lv_bar_set_value(bar, v, anim).
lv_obj_t* pm_ui_meter_bar(lv_obj_t* parent, int min, int max);

// Numeric/grid keypad. The first child is the display label;
// each button has user_data = the key character (cast to void*
// from intptr_t). Apps subscribe to LV_EVENT_CLICKED on each.
typedef void (*pm_ui_keypad_cb)(char key, void* user);
lv_obj_t* pm_ui_keypad(lv_obj_t* parent,
                        const char* layout,    // e.g. "789\n456\n123\n.0=" — \n = row break
                        pm_ui_keypad_cb cb,
                        void* user);

// Log panel (append-only scrollback).
typedef struct pm_ui_log_s pm_ui_log_t;
pm_ui_log_t* pm_ui_log_create(lv_obj_t* parent);
void         pm_ui_log_append(pm_ui_log_t* log, const char* line);
void         pm_ui_log_clear (pm_ui_log_t* log);
lv_obj_t*    pm_ui_log_obj   (pm_ui_log_t* log);

// Quick column-based grid: rows × cols, expands children equally.
lv_obj_t* pm_ui_grid(lv_obj_t* parent, int rows, int cols);

#ifdef __cplusplus
}
#endif

#endif  // PM_UI_H

// ── Default app screen ───────────────────────────────────────
// Convenience for apps whose UI is not yet fully built. Returns
// a screen with titlebar + a single card containing the given
// status text. Apps can keep this until they upgrade.
lv_obj_t* pm_ui_default_screen(const char* app_title,
                                 const char* status_text);

// Update the status text on a default screen. The screen must
// have been created via pm_ui_default_screen().
void      pm_ui_default_screen_set_status(lv_obj_t* scr, const char* text);
