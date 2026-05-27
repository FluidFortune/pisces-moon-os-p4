// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_simcity.c — Tile-based city builder
//
//  Map: 64×40 tiles (P4 has the screen for it).
//  Tools: BULLDOZE / ROAD / RESIDENTIAL / COMMERCIAL /
//         INDUSTRIAL / PARK / POWER
//  Simulation tick: every 2 seconds — population growth,
//  income/expenses, demand calculation.
//
//  Save format: raw tile array + economy state to NoSQL
//  category "simcity_save".
// ============================================================

#include "pm_app_simcity.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_SIMCITY";

#define MAP_W   64
#define MAP_H   40

typedef enum {
    T_GRASS, T_ROAD, T_RES, T_COM, T_IND, T_PARK, T_POWER, T_RUBBLE
} tile_t;

typedef enum {
    TOOL_NONE, TOOL_BULLDOZE, TOOL_ROAD, TOOL_RES, TOOL_COM, TOOL_IND,
    TOOL_PARK, TOOL_POWER
} tool_t;

static uint8_t  s_map[MAP_H][MAP_W];
static int      s_population = 0;
static int      s_cash       = 50000;
static int      s_year       = 1;
static int      s_month      = 1;
static tool_t   s_tool       = TOOL_NONE;
static uint32_t s_last_sim_ms = 0;

// LVGL
static void* s_screen = NULL;
static void* s_canvas = NULL;

// ─────────────────────────────────────────────
//  Costs
// ─────────────────────────────────────────────
static int _tile_cost(tool_t t) {
    switch (t) {
        case TOOL_BULLDOZE: return    -1;     // refunds 0; cost negligible
        case TOOL_ROAD:     return   100;
        case TOOL_RES:      return   500;
        case TOOL_COM:      return   800;
        case TOOL_IND:      return  1000;
        case TOOL_PARK:     return   300;
        case TOOL_POWER:    return  2000;
        default:            return     0;
    }
}

static tile_t _tool_to_tile(tool_t t) {
    switch (t) {
        case TOOL_ROAD:  return T_ROAD;
        case TOOL_RES:   return T_RES;
        case TOOL_COM:   return T_COM;
        case TOOL_IND:   return T_IND;
        case TOOL_PARK:  return T_PARK;
        case TOOL_POWER: return T_POWER;
        default:         return T_GRASS;
    }
}

// ─────────────────────────────────────────────
//  Tools
// ─────────────────────────────────────────────
void pm_app_simcity_set_tool(int tool) { s_tool = (tool_t)tool; }

void pm_app_simcity_tile_tap(int gx, int gy) {
    if (gx < 0 || gx >= MAP_W || gy < 0 || gy >= MAP_H) return;
    if (s_tool == TOOL_NONE) return;
    int cost = _tile_cost(s_tool);
    if (s_cash < cost) return;
    if (s_tool == TOOL_BULLDOZE) {
        s_map[gy][gx] = T_GRASS;
    } else {
        s_map[gy][gx] = _tool_to_tile(s_tool);
        s_cash -= cost;
    }
    // TODO_LVGL: redraw single tile
}

// ─────────────────────────────────────────────
//  Simulation step
// ─────────────────────────────────────────────
static int _count_tiles(uint8_t t) {
    int n = 0;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++)
        if (s_map[y][x] == t) n++;
    return n;
}

static void _sim_step(void) {
    // Calendar
    s_month++;
    if (s_month > 12) { s_month = 1; s_year++; }

    int res = _count_tiles(T_RES);
    int com = _count_tiles(T_COM);
    int ind = _count_tiles(T_IND);
    int roads = _count_tiles(T_ROAD);
    int power = _count_tiles(T_POWER);
    int parks = _count_tiles(T_PARK);

    // Demand fulfilled when there's enough commercial+industrial
    int demand_jobs = res * 4;
    int jobs        = com * 6 + ind * 8;
    int growth_factor = (jobs >= demand_jobs) ? 4 : 1;

    // Each residential tile holds up to 200 people, growth gated on
    // road access (proxy: roads >= res / 3) and power
    int target_pop = 0;
    if (roads * 3 >= res && power > 0) {
        target_pop = res * 200 * growth_factor / 4;
    }
    if (s_population < target_pop) s_population += (target_pop - s_population) / 8 + 1;
    else if (s_population > target_pop) s_population -= (s_population - target_pop) / 16;

    // Income (taxes) - expenses
    int taxes  = s_population / 10;
    int upkeep = roads * 2 + power * 50 + parks * 5;
    s_cash += taxes - upkeep;

    pm_log_d(TAG, "%d/%d pop=%d cash=%d res=%d com=%d ind=%d",
             s_month, s_year, s_population, s_cash, res, com, ind);
}

// ─────────────────────────────────────────────
//  Save / load via NoSQL
// ─────────────────────────────────────────────
void pm_app_simcity_save(void) {
    // Pack: header line + binary map dump as base64 would be cleaner,
    // but for now we write as an ad-hoc text block.
    // size = ~64*40 = 2560 bytes hex; doable.
    char* buf = (char*)pm_psram_alloc(8192);
    if (!buf) return;
    int n = snprintf(buf, 8192,
        "year=%d month=%d pop=%d cash=%d\n",
        s_year, s_month, s_population, s_cash);
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            n += snprintf(buf + n, 8192 - n, "%d ", s_map[y][x]);
        }
        n += snprintf(buf + n, 8192 - n, "\n");
    }
    pm_nosql_write("simcity_save", "current", buf, (size_t)n);
    pm_psram_free(buf);
    pm_log_i(TAG, "saved (%d bytes)", n);
}

void pm_app_simcity_load(void) {
    char* buf = (char*)pm_psram_alloc(8192);
    if (!buf) return;
    size_t got = pm_nosql_read("simcity_save", "current", buf, 8192);
    if (got == 0) { pm_psram_free(buf); return; }

    sscanf(buf, "year=%d month=%d pop=%d cash=%d",
           &s_year, &s_month, &s_population, &s_cash);
    char* p = strchr(buf, '\n');
    if (!p) { pm_psram_free(buf); return; }
    p++;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int v = 0;
            sscanf(p, "%d", &v);
            s_map[y][x] = (uint8_t)v;
            while (*p && *p != ' ' && *p != '\n') p++;
            while (*p == ' ') p++;
        }
        if (*p == '\n') p++;
    }
    pm_psram_free(buf);
    pm_log_i(TAG, "loaded");
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static void _new_city(void) {
    memset(s_map, T_GRASS, sizeof(s_map));
    s_population = 0; s_cash = 50000; s_year = 1; s_month = 1;
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("SIMCITY",
        "SIMCITY app — UI ready");
}
static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _new_city();
    s_last_sim_ms = pm_millis();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_sim_ms < 2000) return;
    s_last_sim_ms = now;
    _sim_step();
    // TODO_LVGL: refresh HUD
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "simcity",
    .display_name = "SIMCITY",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_simcity(void) { return &_APP; }
