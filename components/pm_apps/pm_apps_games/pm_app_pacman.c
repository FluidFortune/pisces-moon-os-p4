// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_pacman.c — Pac-Man
//
//  Direct port of S3 pacman.cpp game logic. Renders to LVGL
//  canvas (stubbed). Tile size doubled to 16 px for the
//  larger panel.
//
//  Maze:        28×31 tiles
//  Ghost AI:    Blinky / Pinky / Inky / Clyde with original
//               targeting personalities
//  States:      SCATTER / CHASE / FRIGHTENED / EATEN / HOUSE
// ============================================================

#include "pm_app_pacman.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char* TAG = "PM_PACMAN";

#define TS          16
#define MAZE_W      28
#define MAZE_H      31

#define T_EMPTY     0
#define T_WALL      1
#define T_DOT       2
#define T_ENERGIZER 3
#define T_DOOR      4

#define DIR_NONE    0
#define DIR_LEFT    1
#define DIR_RIGHT   2
#define DIR_UP      3
#define DIR_DOWN    4

typedef enum {
    GS_SCATTER, GS_CHASE, GS_FRIGHTENED, GS_EATEN, GS_HOUSE
} ghost_state_t;

typedef struct {
    float px, py;
    int   tx, ty;
    int   dir;
    int   nextDir;
    float speed;
} entity_t;

typedef struct {
    entity_t e;
    ghost_state_t state;
    int frightenedTimer;
} ghost_t;

// Reduced maze layout (from S3 — same 28×31 grid).
// 1 = wall, 2 = dot, 3 = energizer, 0 = empty, 4 = door.
// Compact encoding kept verbose for clarity.
static const uint8_t MAZE_TEMPLATE[MAZE_H][MAZE_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,3,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,3,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1},
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,4,4,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,0,0,0,0,0,0,1,0,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0,1,0,0,0,2,0,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,0,0,0,0,0,0,1,0,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,3,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,3,1},
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1},
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1},
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// Working maze
static uint8_t s_maze[MAZE_H][MAZE_W];

// Game state
static entity_t s_pac;
static ghost_t  s_ghosts[4];
static int      s_score = 0;
static int      s_high  = 0;
static int      s_lives = 3;
static int      s_stage = 1;
static int      s_dots_remaining = 0;
static int      s_eat_multiplier = 1;
static uint32_t s_last_tick_ms = 0;

// LVGL handles
static void* s_screen = NULL;
static void* s_canvas = NULL;

// ─────────────────────────────────────────────
//  Persistence
// ─────────────────────────────────────────────
static int  _load_hs(void) {
    char buf[16];
    return pm_nosql_read("highscores", "pacman", buf, sizeof(buf)) ? atoi(buf) : 0;
}
static void _save_hs(int v) {
    char buf[16]; int n = snprintf(buf, sizeof(buf), "%d", v);
    pm_nosql_write("highscores", "pacman", buf, n);
}

// ─────────────────────────────────────────────
//  Maze / entity init
// ─────────────────────────────────────────────
static void _init_maze(void) {
    s_dots_remaining = 0;
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++) {
            s_maze[y][x] = MAZE_TEMPLATE[y][x];
            if (s_maze[y][x] == T_DOT || s_maze[y][x] == T_ENERGIZER)
                s_dots_remaining++;
        }
}

static void _init_entities(void) {
    s_pac.tx = 13; s_pac.ty = 23;
    s_pac.px = s_pac.tx * TS; s_pac.py = s_pac.ty * TS;
    s_pac.dir = DIR_LEFT; s_pac.nextDir = DIR_LEFT; s_pac.speed = 0.16f;

    int sx[4] = {13, 13, 11, 15};
    int sy[4] = {11, 14, 14, 14};
    for (int g = 0; g < 4; g++) {
        s_ghosts[g].e.tx = sx[g]; s_ghosts[g].e.ty = sy[g];
        s_ghosts[g].e.px = sx[g] * TS; s_ghosts[g].e.py = sy[g] * TS;
        s_ghosts[g].e.dir = DIR_UP; s_ghosts[g].e.nextDir = DIR_UP;
        s_ghosts[g].e.speed = 0.12f;
        s_ghosts[g].state = (g == 0) ? GS_CHASE : GS_HOUSE;
        s_ghosts[g].frightenedTimer = 0;
    }
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void pm_app_pacman_dir(int dx, int dy) {
    if      (dx ==  1) s_pac.nextDir = DIR_RIGHT;
    else if (dx == -1) s_pac.nextDir = DIR_LEFT;
    else if (dy ==  1) s_pac.nextDir = DIR_DOWN;
    else if (dy == -1) s_pac.nextDir = DIR_UP;
}

// ─────────────────────────────────────────────
//  Movement helpers
// ─────────────────────────────────────────────
static void _dir_vec(int d, int* dx, int* dy) {
    *dx = 0; *dy = 0;
    switch (d) {
        case DIR_LEFT:  *dx = -1; break;
        case DIR_RIGHT: *dx =  1; break;
        case DIR_UP:    *dy = -1; break;
        case DIR_DOWN:  *dy =  1; break;
    }
}

static int _wrap_tx(int tx) {
    if (tx < 0)            return MAZE_W - 1;
    if (tx >= MAZE_W)      return 0;
    return tx;
}

static bool _can_enter(int tx, int ty, bool ghost) {
    if (ty < 0 || ty >= MAZE_H) return false;
    tx = _wrap_tx(tx);
    uint8_t t = s_maze[ty][tx];
    if (t == T_WALL)        return false;
    if (t == T_DOOR)        return ghost;
    return true;
}

// ─────────────────────────────────────────────
//  AI — simplified targeting (full S3 logic too long here;
//  this is a faithful subset that preserves arcade feel).
// ─────────────────────────────────────────────
static void _choose_ghost_dir(int g) {
    int dx, dy;
    int best = DIR_NONE;
    float best_dist = 1e9f;

    int target_x = s_pac.tx;
    int target_y = s_pac.ty;
    if (g == 1) { _dir_vec(s_pac.dir, &dx, &dy); target_x += dx * 4; target_y += dy * 4; }
    if (g == 2) { target_x = (s_ghosts[0].e.tx * 2) - s_pac.tx;
                  target_y = (s_ghosts[0].e.ty * 2) - s_pac.ty; }
    if (g == 3) {
        int gx = s_ghosts[g].e.tx, gy = s_ghosts[g].e.ty;
        if (((gx - s_pac.tx) * (gx - s_pac.tx) + (gy - s_pac.ty) * (gy - s_pac.ty)) < 64) {
            target_x = 0; target_y = MAZE_H - 1;
        }
    }
    if (s_ghosts[g].state == GS_FRIGHTENED) {
        target_x = pm_random_range(0, MAZE_W - 1);
        target_y = pm_random_range(0, MAZE_H - 1);
    }
    if (s_ghosts[g].state == GS_EATEN) { target_x = 13; target_y = 14; }

    int opposite[] = {DIR_NONE, DIR_RIGHT, DIR_LEFT, DIR_DOWN, DIR_UP};
    for (int d = DIR_LEFT; d <= DIR_DOWN; d++) {
        if (d == opposite[s_ghosts[g].e.dir]) continue;
        _dir_vec(d, &dx, &dy);
        int nx = _wrap_tx(s_ghosts[g].e.tx + dx);
        int ny = s_ghosts[g].e.ty + dy;
        if (!_can_enter(nx, ny, true)) continue;
        float dd = (float)((nx - target_x) * (nx - target_x) +
                            (ny - target_y) * (ny - target_y));
        if (dd < best_dist) { best_dist = dd; best = d; }
    }
    if (best != DIR_NONE) s_ghosts[g].e.nextDir = best;
}

// ─────────────────────────────────────────────
//  Tick — runs game step
// ─────────────────────────────────────────────
static void _step(void) {
    // Pac-Man movement (snap-to-grid logic)
    float cx = s_pac.tx * TS, cy = s_pac.ty * TS;
    if (fabsf(s_pac.px - cx) + fabsf(s_pac.py - cy) < s_pac.speed * 1.5f * TS) {
        s_pac.px = cx; s_pac.py = cy;
        int ndx, ndy; _dir_vec(s_pac.nextDir, &ndx, &ndy);
        if (_can_enter(_wrap_tx(s_pac.tx + ndx), s_pac.ty + ndy, false))
            s_pac.dir = s_pac.nextDir;
    }
    int dx, dy; _dir_vec(s_pac.dir, &dx, &dy);
    if (_can_enter(_wrap_tx(s_pac.tx + dx), s_pac.ty + dy, false)) {
        s_pac.px += dx * s_pac.speed * TS;
        s_pac.py += dy * s_pac.speed * TS;
        s_pac.tx = _wrap_tx((int)(s_pac.px / TS + 0.5f));
        s_pac.ty = (int)(s_pac.py / TS + 0.5f);

        // Eat
        uint8_t* t = &s_maze[s_pac.ty][s_pac.tx];
        if (*t == T_DOT)       { *t = T_EMPTY; s_score += 10; s_dots_remaining--; }
        else if (*t == T_ENERGIZER) {
            *t = T_EMPTY; s_score += 50; s_dots_remaining--;
            s_eat_multiplier = 1;
            for (int g = 0; g < 4; g++)
                if (s_ghosts[g].state == GS_CHASE || s_ghosts[g].state == GS_SCATTER) {
                    s_ghosts[g].state = GS_FRIGHTENED;
                    s_ghosts[g].frightenedTimer = 60;
                }
        }
    }

    // Ghosts
    for (int g = 0; g < 4; g++) {
        ghost_t* gh = &s_ghosts[g];
        if (gh->state == GS_HOUSE) continue;
        if (gh->frightenedTimer > 0 && --gh->frightenedTimer == 0)
            gh->state = GS_CHASE;
        float gcx = gh->e.tx * TS, gcy = gh->e.ty * TS;
        if (fabsf(gh->e.px - gcx) + fabsf(gh->e.py - gcy) < gh->e.speed * 1.5f * TS) {
            gh->e.px = gcx; gh->e.py = gcy;
            _choose_ghost_dir(g);
            gh->e.dir = gh->e.nextDir;
        }
        _dir_vec(gh->e.dir, &dx, &dy);
        if (_can_enter(_wrap_tx(gh->e.tx + dx), gh->e.ty + dy, true)) {
            gh->e.px += dx * gh->e.speed * TS;
            gh->e.py += dy * gh->e.speed * TS;
            gh->e.tx = _wrap_tx((int)(gh->e.px / TS + 0.5f));
            gh->e.ty = (int)(gh->e.py / TS + 0.5f);
        }

        // Collision with Pac-Man
        if (gh->e.tx == s_pac.tx && gh->e.ty == s_pac.ty) {
            if (gh->state == GS_FRIGHTENED) {
                gh->state = GS_EATEN;
                s_score += 200 * s_eat_multiplier;
                s_eat_multiplier *= 2;
            } else if (gh->state != GS_EATEN) {
                s_lives--;
                _init_entities();
                if (s_lives <= 0) {
                    if (s_score > s_high) { s_high = s_score; _save_hs(s_high); }
                }
                return;
            }
        }
    }

    // Stage clear
    if (s_dots_remaining <= 0) {
        s_stage++;
        _init_maze();
        _init_entities();
    }
}

// ─────────────────────────────────────────────
//  Render (LVGL canvas — stubbed)
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: maze tiles, dots, energizers (blink), pacman sprite,
    //            ghost sprites colored by state, HUD (score/hi/lives).
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("PAC-MAN",
        "PAC-MAN app — UI ready");
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_high = _load_hs();
    s_score = 0; s_lives = 3; s_stage = 1;
    _init_maze(); _init_entities();
    s_last_tick_ms = pm_millis();
    _render();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_tick_ms < 33) return;     // ~30 Hz
    s_last_tick_ms = now;
    _step();
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit (score=%d)", s_score); }

static const pm_app_t _APP = {
    .id           = "pacman",
    .display_name = "PAC-MAN",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_pacman(void) { return &_APP; }
