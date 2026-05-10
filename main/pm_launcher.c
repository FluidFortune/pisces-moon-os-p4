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
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_LAUNCHER";

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
                              lv_event_cb_t cb, void* user) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 200, 110);
    lv_obj_set_style_bg_color(tile, PM_C_BG_2, 0);
    lv_obj_set_style_bg_opa  (tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius  (tile, 10, 0);
    lv_obj_set_style_pad_all (tile, 12, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(accent_rgb), 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_text_color(tile, PM_C_FG, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* t = lv_label_create(tile);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(accent_rgb), 0);

    if (sub) {
        lv_obj_t* s = lv_label_create(tile);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_color(s, PM_C_FG_DIM, 0);
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
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(body, 12, 0);
    lv_obj_set_style_pad_all(body, 12, 0);

    for (int c = 0; c < PM_CAT_COUNT; c++) {
        char sub[24];
        snprintf(sub, sizeof(sub), "%d apps", pm_app_count_in_category(c));
        _make_tile(body, s_cat_meta[c].name, sub, s_cat_meta[c].accent_rgb,
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

    s_apps_grid = lv_obj_create(s_scr_apps);
    lv_obj_remove_style_all(s_apps_grid);
    lv_obj_set_size(s_apps_grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(s_apps_grid, 1);
    lv_obj_set_layout(s_apps_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_apps_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_apps_grid, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_apps_grid, 10, 0);
    lv_obj_set_style_pad_all(s_apps_grid, 12, 0);
}

static void _populate_apps_for_category(pm_category_t cat) {
    if (!s_apps_grid) return;
    lv_obj_clean(s_apps_grid);

    if (s_apps_titlebar_label)
        lv_label_set_text(s_apps_titlebar_label, s_cat_meta[cat].name);

    int n = pm_app_count_in_category(cat);
    uint32_t accent = s_cat_meta[cat].accent_rgb;
    for (int i = 0; i < n; i++) {
        const pm_app_t* a = pm_app_in_category(cat, i);
        if (!a) continue;
        _make_tile(s_apps_grid, a->display_name, a->id, accent,
                    _app_tile_clicked, (void*)a);
    }
}

// ─────────────────────────────────────────────
//  Init / show / hide
// ─────────────────────────────────────────────
void pm_launcher_init(void) {
    pm_log_i(TAG, "launcher init");
    _build_cats_screen();
    _build_apps_screen();
    s_view = PM_LAUNCHER_VIEW_CATEGORIES;
    lv_screen_load(s_scr_cats);
}

void pm_launcher_show(void) {
    pm_log_i(TAG, "show (view=%d)", (int)s_view);
    lv_screen_load(s_view == PM_LAUNCHER_VIEW_APPS ? s_scr_apps : s_scr_cats);
}

void pm_launcher_hide(void) { /* noop — apps load their own screens */ }

// ─────────────────────────────────────────────
//  Navigation
// ─────────────────────────────────────────────
pm_launcher_view_t pm_launcher_current_view(void) { return s_view; }

void pm_launcher_open_category(pm_category_t cat) {
    if (cat < 0 || cat >= PM_CAT_COUNT) return;
    s_open_category = cat;
    s_app_page      = 0;
    s_view          = PM_LAUNCHER_VIEW_APPS;
    pm_log_i(TAG, "open category '%s' (%d apps)",
             s_cat_meta[cat].name, pm_app_count_in_category(cat));
    _populate_apps_for_category(cat);
    pm_launcher_show();
}

void pm_launcher_back_to_categories(void) {
    s_view = PM_LAUNCHER_VIEW_CATEGORIES;
    pm_launcher_show();
}

void pm_launcher_open_app(const pm_app_t* app) {
    if (!app) return;
    pm_log_i(TAG, "open app '%s'", app->id);
    pm_launcher_hide();
    pm_app_set_current(app);
    if (app->enter) app->enter();
}

void pm_launcher_back_from_app(void) {
    const pm_app_t* app = pm_app_current();
    if (app) {
        pm_log_i(TAG, "exit app '%s'", app->id);
        if (app->exit) app->exit();
        pm_app_set_current(NULL);
    }
    pm_launcher_show();
}
