// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_launcher.c — LVGL-driven launcher
//
//  Two screens, swapped via lv_screen_load:
//    s_scr_cats  — 7 category tiles (4-col grid)
//    s_scr_apps  — paged app tile grid for the chosen category
//
//  Apps are responsible for building their own screens at
//  init() time; pm_launcher_open_app() just calls enter()
//  and the app loads its screen. Back from an app re-shows
//  whichever launcher view we were last on.
// ============================================================

#include "pm_launcher.h"
#include "pm_hal.h"
#include "pm_app.h"
#include "pm_ui.h"
#include "pm_cyber.h"
#include "pm_board.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_LAUNCHER";

// ────────────────────────────────────────────
//  Cyberpunk tile sizing — adapts to 5"/7" panel
//
//  Categories: 3-across grid, ~3 rows for 7 tiles.
//  Apps:       4-across grid, paged via vertical scroll.
// ────────────────────────────────────────────
#if PM_BOARD_LCD_H_RES <= 800
#  define LAUNCHER_TILE_GAP        12
#  define LAUNCHER_CAT_TILE_W      170
#  define LAUNCHER_CAT_TILE_H      150
#  define LAUNCHER_APP_TILE_W      150
#  define LAUNCHER_APP_TILE_H      140
#  define LAUNCHER_TITLE_FONT      (&lv_font_montserrat_18)
#  define LAUNCHER_SUB_FONT        (&lv_font_montserrat_12)
#  define LAUNCHER_HEADER_FONT     (&lv_font_montserrat_18)
#  define LAUNCHER_TITLEBAR_H      38
#else
#  define LAUNCHER_TILE_GAP        18
#  define LAUNCHER_CAT_TILE_W      220
#  define LAUNCHER_CAT_TILE_H      200
#  define LAUNCHER_APP_TILE_W      190
#  define LAUNCHER_APP_TILE_H      170
#  define LAUNCHER_TITLE_FONT      (&lv_font_montserrat_20)
#  define LAUNCHER_SUB_FONT        (&lv_font_montserrat_14)
#  define LAUNCHER_HEADER_FONT     (&lv_font_montserrat_24)
#  define LAUNCHER_TITLEBAR_H      44
#endif

// ─────────────────────────────────────────────
//  Category metadata — colors track S3 accents.
// ─────────────────────────────────────────────
static const pm_category_meta_t s_cat_meta[PM_CAT_COUNT] = {
    [PM_CAT_COMMS]  = { "COMMS",  "C", 0x00BFCF },
    [PM_CAT_CYBER]  = { "CYBER",  "X", 0xFF6000 },
    [PM_CAT_TOOLS]  = { "TOOLS",  "T", 0xFFA800 },
    [PM_CAT_GAMES]  = { "GAMES",  "G", 0x00FF00 },
    [PM_CAT_INTEL]  = { "INTEL",  "I", 0x00FFFF },
    [PM_CAT_MEDIA]  = { "MEDIA",  "M", 0xFF00FF },
    [PM_CAT_SYSTEM] = { "SYSTEM", "S", 0x808080 },
};

const pm_category_meta_t* pm_category_meta(pm_category_t cat) {
    if (cat < 0 || cat >= PM_CAT_COUNT) return NULL;
    return &s_cat_meta[cat];
}

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
static pm_launcher_view_t s_view = PM_LAUNCHER_VIEW_CATEGORIES;
static pm_category_t      s_open_category = PM_CAT_SYSTEM;
static int                s_app_page      = 0;

static lv_obj_t* s_scr_cats = NULL;
static lv_obj_t* s_scr_apps = NULL;
static lv_obj_t* s_apps_titlebar_label = NULL;
static lv_obj_t* s_apps_grid = NULL;
static bool      s_app_inited[64];

static void _ensure_app_inited(const pm_app_t* app) {
    if (!app) return;
    int n = pm_app_count();
    for (int i = 0; i < n; i++) {
        if (pm_app_at(i) != app) continue;
        if (i >= (int)(sizeof(s_app_inited) / sizeof(s_app_inited[0]))) return;
        if (s_app_inited[i]) return;
        s_app_inited[i] = true;
        if (app->init) {
            pm_log_i(TAG, "init app '%s'", app->id);
            app->init();
        }
        return;
    }
}

// ─────────────────────────────────────────────
//  Tile builders
// ─────────────────────────────────────────────
static void _cat_tile_clicked(lv_event_t* e) {
    pm_category_t cat = (pm_category_t)(intptr_t)lv_event_get_user_data(e);
    pm_launcher_open_category(cat);
}

static void _app_tile_clicked(lv_event_t* e) {
    const pm_app_t* app = (const pm_app_t*)lv_event_get_user_data(e);
    pm_launcher_open_app(app);
}

static void _back_to_cats_cb(lv_event_t* e) {
    (void)e;
    pm_launcher_back_to_categories();
}

static lv_obj_t* _make_tile(lv_obj_t* parent, const char* title,
                              const char* sub, uint32_t accent_rgb,
                              int width, int height,
                              lv_event_cb_t cb, void* user) {
    lv_color_t accent = lv_color_hex(accent_rgb);

    // Chamfered tile with category-color border. pm_cyber_make_tile
    // already wires flex-column / centered children / scroll-off, so
    // we just drop in the labels.
    lv_obj_t* tile = pm_cyber_make_tile(parent, width, height, accent, false);

    lv_obj_t* t = lv_label_create(tile);
    lv_label_set_text(t, title ? title : "");
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_style_text_color(t, accent, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, LAUNCHER_TITLE_FONT, 0);
    lv_obj_set_style_text_letter_space(t, 2, 0);

    if (sub) {
        lv_obj_t* s = lv_label_create(tile);
        lv_label_set_text(s, sub);
        lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s, LV_PCT(100));
        // Subtitle in matrix green for the circuit feel.
        lv_obj_set_style_text_color(s, PM_CYBER_C_MATRIX, 0);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s, LAUNCHER_SUB_FONT, 0);
    }

    if (cb) lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, user);
    return tile;
}

// ─────────────────────────────────────────────
//  Build categories screen
// ─────────────────────────────────────────────
static void _build_cats_screen(void) {
    s_scr_cats = pm_ui_screen();

    lv_obj_t* bar = pm_ui_titlebar(s_scr_cats, "PISCES MOON", NULL, NULL);
    // Suppress back button on top-level screen
    lv_obj_t* back = lv_obj_get_child(bar, 0);
    if (back) lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* body = lv_obj_create(s_scr_cats);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Matrix-green PCB substrate behind the tiles. Drawn first so
    // every child added afterwards (the tiles) renders on top.
    pm_cyber_draw_pcb_bg(body,
                          PM_BOARD_LCD_H_RES,
                          PM_BOARD_LCD_V_RES - LAUNCHER_TITLEBAR_H);

    // Tile grid layer on top of the PCB.
    lv_obj_t* grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(grid, LAUNCHER_TILE_GAP, 0);
    lv_obj_set_style_pad_all(grid, LAUNCHER_TILE_GAP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int c = 0; c < PM_CAT_COUNT; c++) {
        char sub[24];
        snprintf(sub, sizeof(sub), "%d apps", pm_app_count_in_category(c));
        _make_tile(grid, s_cat_meta[c].name, sub, s_cat_meta[c].accent_rgb,
                    LAUNCHER_CAT_TILE_W, LAUNCHER_CAT_TILE_H,
                    _cat_tile_clicked, (void*)(intptr_t)c);
    }
}

// ─────────────────────────────────────────────
//  Build apps screen — rebuilds tiles when category changes
// ─────────────────────────────────────────────
static void _build_apps_screen(void) {
    s_scr_apps = pm_ui_screen();

    pm_ui_titlebar(s_scr_apps, "", _back_to_cats_cb, NULL);
    s_apps_titlebar_label =
        lv_obj_get_child(lv_obj_get_child(s_scr_apps, 0), 1);

    // Body holds the PCB substrate + the scrollable grid.
    lv_obj_t* body = lv_obj_create(s_scr_apps);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Matrix-green PCB substrate behind the tile grid.
    pm_cyber_draw_pcb_bg(body,
                          PM_BOARD_LCD_H_RES,
                          PM_BOARD_LCD_V_RES - LAUNCHER_TITLEBAR_H);

    // Scrollable tile grid layer.
    s_apps_grid = lv_obj_create(body);
    lv_obj_remove_style_all(s_apps_grid);
    lv_obj_set_size(s_apps_grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_apps_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(s_apps_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_apps_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_apps_grid, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_apps_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_apps_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_apps_grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_gap(s_apps_grid, LAUNCHER_TILE_GAP, 0);
    lv_obj_set_style_pad_all(s_apps_grid, LAUNCHER_TILE_GAP, 0);
    lv_obj_set_style_pad_bottom(s_apps_grid, LAUNCHER_TILE_GAP * 2, 0);
}

static void _populate_apps_for_category(pm_category_t cat) {
    if (!s_apps_grid) return;
    lv_obj_clean(s_apps_grid);

    if (s_apps_titlebar_label) {
        char title[48];
        snprintf(title, sizeof(title), "%s  %d APPS",
                 s_cat_meta[cat].name, pm_app_count_in_category(cat));
        lv_label_set_text(s_apps_titlebar_label, title);
    }

    int n = pm_app_count_in_category(cat);
    uint32_t accent = s_cat_meta[cat].accent_rgb;
    for (int i = 0; i < n; i++) {
        const pm_app_t* a = pm_app_in_category(cat, i);
        if (!a) continue;
        _make_tile(s_apps_grid, a->display_name, a->id, accent,
                    LAUNCHER_APP_TILE_W, LAUNCHER_APP_TILE_H,
                    _app_tile_clicked, (void*)a);
        if ((i & 3) == 3) {
            pm_delay_ms(1);
        }
    }
    lv_obj_scroll_to_y(s_apps_grid, 0, LV_ANIM_OFF);
}

// ─────────────────────────────────────────────
//  Init / show / hide
// ─────────────────────────────────────────────
void pm_launcher_init(void) {
    pm_log_i(TAG, "launcher init");
    if (!lvgl_port_lock(0)) return;
    _build_cats_screen();
    _build_apps_screen();
    s_view = PM_LAUNCHER_VIEW_CATEGORIES;
    lvgl_port_unlock();
}

void pm_launcher_show(void) {
    pm_log_i(TAG, "show (view=%d)", (int)s_view);
    if (!lvgl_port_lock(0)) return;
    lv_screen_load(s_view == PM_LAUNCHER_VIEW_APPS ? s_scr_apps : s_scr_cats);
    lvgl_port_unlock();
}

void pm_launcher_hide(void) { /* noop — apps load their own screens */ }

// ─────────────────────────────────────────────
//  Navigation
// ─────────────────────────────────────────────
pm_launcher_view_t pm_launcher_current_view(void) { return s_view; }

void pm_launcher_open_category(pm_category_t cat) {
    if (cat < 0 || cat >= PM_CAT_COUNT) return;
    if (!lvgl_port_lock(0)) return;
    s_open_category = cat;
    s_app_page      = 0;
    s_view          = PM_LAUNCHER_VIEW_APPS;
    pm_log_i(TAG, "open category '%s' (%d apps)",
             s_cat_meta[cat].name, pm_app_count_in_category(cat));
    _populate_apps_for_category(cat);
    lv_screen_load(s_scr_apps);
    lvgl_port_unlock();
}

void pm_launcher_back_to_categories(void) {
    if (!lvgl_port_lock(0)) return;
    s_view = PM_LAUNCHER_VIEW_CATEGORIES;
    lv_screen_load(s_scr_cats);
    lvgl_port_unlock();
}

void pm_launcher_open_app(const pm_app_t* app) {
    if (!app) return;
    pm_log_i(TAG, "open app '%s'", app->id);
    if (!lvgl_port_lock(0)) return;
    const pm_app_t* cur = pm_app_current();
    if (cur && cur->exit) {
        pm_log_i(TAG, "leave foreground app '%s'", cur->id);
        cur->exit();
        pm_app_set_current(NULL);
    }
    pm_launcher_hide();
    _ensure_app_inited(app);
    pm_app_set_current(app);
    if (app->enter) app->enter();
    lvgl_port_unlock();
}

void pm_launcher_back_from_app(void) {
    if (!lvgl_port_lock(0)) return;
    const pm_app_t* app = pm_app_current();
    if (app) {
        pm_log_i(TAG, "exit app '%s'", app->id);
        if (app->exit) app->exit();
        pm_app_set_current(NULL);
    }
    lv_screen_load(s_view == PM_LAUNCHER_VIEW_APPS ? s_scr_apps : s_scr_cats);
    lvgl_port_unlock();
}
