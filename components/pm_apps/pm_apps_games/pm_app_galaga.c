// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_galaga.c — Vertical shooter (Galaga-style)
//
//  Logic carryover from S3 with these arcade properties:
//    - Player ship at bottom, x-axis movement only
//    - Enemy formation arrives in waves
//    - Dive attacks at random intervals
//    - Player bullets, enemy bullets
//    - Explosion queue (non-blocking — was the v1.1.0 fix)
//
//  Differential HUD redraw: dirty-tracked score/lives/stage
//  so the header doesn't flash every frame (v1.0.0 fix).
// ============================================================

#include "pm_app_galaga.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char* TAG = "PM_GALAGA";

// Field dimensions on P4 (1024×600). Header on top, ship at bottom.
#define FIELD_W      1024
#define FIELD_H      600
#define HUD_H         32
#define SHIP_W        32
#define SHIP_H        24
#define ENEMY_W       28
#define ENEMY_H       20
#define BULLET_W       4
#define BULLET_H      12

#define MAX_ENEMIES   32
#define MAX_PBULLETS   8
#define MAX_EBULLETS  16
#define MAX_EXPLOSIONS 16

typedef struct {
    bool active;
    float x, y;
    float vx, vy;
    int   formation_x, formation_y;
    int   state;     // 0=arrive, 1=formation, 2=dive
    int   timer;
    int   type;      // 0=grunt, 1=mid, 2=boss
} enemy_t;

typedef struct {
    bool active;
    float x, y;
} bullet_t;

typedef struct {
    bool active;
    float x, y;
    int   frame;     // 0..N
    uint32_t last_ms;
} explosion_t;

// Game state
static float        s_ship_x   = FIELD_W / 2.0f;
static int          s_ship_dx  = 0;
static int          s_score    = 0;
static int          s_high     = 0;
static int          s_lives    = 3;
static int          s_stage    = 1;
static enemy_t      s_enemies[MAX_ENEMIES];
static bullet_t     s_pbullets[MAX_PBULLETS];
static bullet_t     s_ebullets[MAX_EBULLETS];
static explosion_t  s_explosions[MAX_EXPLOSIONS];
static uint32_t     s_last_step_ms = 0;
static int          s_last_hud_score = -1, s_last_hud_lives = -1, s_last_hud_stage = -1;

// LVGL handles
static void* s_screen = NULL;
static void* s_canvas = NULL;

// ─────────────────────────────────────────────
//  Persistence
// ─────────────────────────────────────────────
static int  _load_hs(void) { char b[16]; return pm_nosql_read("highscores","galaga",b,16) ? atoi(b) : 0; }
static void _save_hs(int v){ char b[16]; int n=snprintf(b,16,"%d",v); pm_nosql_write("highscores","galaga",b,n); }

// ─────────────────────────────────────────────
//  Spawn / reset
// ─────────────────────────────────────────────
static void _spawn_wave(void) {
    // 4 rows × 8 columns formation, centered
    int rows = 4, cols = 8;
    float gx = 80;
    float startX = (FIELD_W - cols * gx) / 2.0f;
    float startY = HUD_H + 30;
    int idx = 0;
    for (int r = 0; r < rows && idx < MAX_ENEMIES; r++) {
        for (int c = 0; c < cols && idx < MAX_ENEMIES; c++, idx++) {
            enemy_t* e = &s_enemies[idx];
            e->active = true;
            e->formation_x = (int)(startX + c * gx);
            e->formation_y = (int)(startY + r * 40);
            // Arrive from off-screen
            e->x = (c % 2 == 0) ? -ENEMY_W : (float)FIELD_W;
            e->y = -ENEMY_H;
            e->vx = ((c % 2 == 0) ? 1 : -1) * 4.0f;
            e->vy = 2.0f;
            e->state = 0;
            e->timer = 0;
            e->type = (r == 0) ? 2 : (r < 2 ? 1 : 0);
        }
    }
    for (; idx < MAX_ENEMIES; idx++) s_enemies[idx].active = false;
}

static void _reset_game(void) {
    s_ship_x = FIELD_W / 2.0f;
    s_score  = 0; s_lives = 3; s_stage = 1;
    memset(s_pbullets,    0, sizeof(s_pbullets));
    memset(s_ebullets,    0, sizeof(s_ebullets));
    memset(s_explosions,  0, sizeof(s_explosions));
    _spawn_wave();
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void pm_app_galaga_dir(int dx) { s_ship_dx = dx; }

void pm_app_galaga_fire(void) {
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!s_pbullets[i].active) {
            s_pbullets[i].active = true;
            s_pbullets[i].x = s_ship_x;
            s_pbullets[i].y = FIELD_H - SHIP_H - 8;
            return;
        }
    }
}

// ─────────────────────────────────────────────
//  Step
// ─────────────────────────────────────────────
static void _spawn_explosion(float x, float y) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!s_explosions[i].active) {
            s_explosions[i].active  = true;
            s_explosions[i].x       = x;
            s_explosions[i].y       = y;
            s_explosions[i].frame   = 0;
            s_explosions[i].last_ms = pm_millis();
            return;
        }
    }
}

static void _step(void) {
    // Ship
    s_ship_x += s_ship_dx * 8.0f;
    if (s_ship_x < SHIP_W / 2)             s_ship_x = SHIP_W / 2;
    if (s_ship_x > FIELD_W - SHIP_W / 2)   s_ship_x = FIELD_W - SHIP_W / 2;

    // Player bullets
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!s_pbullets[i].active) continue;
        s_pbullets[i].y -= 14.0f;
        if (s_pbullets[i].y < 0) s_pbullets[i].active = false;
    }

    // Enemies
    bool any_alive = false;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t* e = &s_enemies[i];
        if (!e->active) continue;
        any_alive = true;
        if (e->state == 0) {       // arrive → formation slot
            float dx = e->formation_x - e->x;
            float dy = e->formation_y - e->y;
            float d  = dx * dx + dy * dy;
            if (d < 100) { e->x = e->formation_x; e->y = e->formation_y; e->state = 1; }
            else { e->x += dx * 0.05f; e->y += dy * 0.05f; }
        } else if (e->state == 1) { // hold formation
            e->x = e->formation_x + 8.0f * sinf(pm_millis() * 0.002f + i);
            // Random dive
            if (pm_random_range(0, 3000) < 2) {
                e->state = 2;
                e->vx = ((s_ship_x - e->x) > 0 ? 4.0f : -4.0f);
                e->vy = 6.0f;
            }
            // Random fire
            if (pm_random_range(0, 6000) < 2) {
                for (int b = 0; b < MAX_EBULLETS; b++)
                    if (!s_ebullets[b].active) {
                        s_ebullets[b].active = true;
                        s_ebullets[b].x = e->x; s_ebullets[b].y = e->y + ENEMY_H;
                        break;
                    }
            }
        } else if (e->state == 2) { // dive
            e->x += e->vx; e->y += e->vy;
            if (e->y > FIELD_H + ENEMY_H) {
                // Re-enter from top, return to formation
                e->y = -ENEMY_H;
                e->x = e->formation_x;
                e->state = 0;
            }
        }

        // Player bullet collision
        for (int b = 0; b < MAX_PBULLETS; b++) {
            if (!s_pbullets[b].active) continue;
            if (s_pbullets[b].x > e->x - ENEMY_W/2 && s_pbullets[b].x < e->x + ENEMY_W/2 &&
                s_pbullets[b].y > e->y - ENEMY_H/2 && s_pbullets[b].y < e->y + ENEMY_H/2) {
                s_pbullets[b].active = false;
                e->active = false;
                int pts = (e->type == 2) ? 200 : (e->type == 1 ? 100 : 50);
                s_score += pts;
                _spawn_explosion(e->x, e->y);
                break;
            }
        }
    }
    if (!any_alive) { s_stage++; _spawn_wave(); }

    // Enemy bullets
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!s_ebullets[i].active) continue;
        s_ebullets[i].y += 9.0f;
        if (s_ebullets[i].y > FIELD_H) { s_ebullets[i].active = false; continue; }
        // Hit ship?
        if (s_ebullets[i].x > s_ship_x - SHIP_W/2 && s_ebullets[i].x < s_ship_x + SHIP_W/2 &&
            s_ebullets[i].y > FIELD_H - SHIP_H && s_ebullets[i].y < FIELD_H) {
            s_ebullets[i].active = false;
            _spawn_explosion(s_ship_x, FIELD_H - SHIP_H/2);
            s_lives--;
            if (s_lives <= 0) {
                if (s_score > s_high) { s_high = s_score; _save_hs(s_high); }
                _reset_game();
                return;
            }
        }
    }

    // Explosions
    uint32_t now = pm_millis();
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!s_explosions[i].active) continue;
        if (now - s_explosions[i].last_ms > 60) {
            s_explosions[i].frame++;
            s_explosions[i].last_ms = now;
            if (s_explosions[i].frame >= 6) s_explosions[i].active = false;
        }
    }
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_hud(void) {
    if (s_score == s_last_hud_score && s_lives == s_last_hud_lives && s_stage == s_last_hud_stage) return;
    s_last_hud_score = s_score; s_last_hud_lives = s_lives; s_last_hud_stage = s_stage;
    // TODO_LVGL: redraw HUD strip only
}

static void _render(void) {
    _render_hud();
    // TODO_LVGL: clear playfield, draw enemies, bullets, ship, explosion sprites.
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("GALAGA",
        "GALAGA app — UI ready");
}
static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_high = _load_hs();
    _reset_game();
    s_last_step_ms = pm_millis();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_step_ms < 33) return;
    s_last_step_ms = now;
    _step();
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit (score=%d)", s_score); }

static const pm_app_t _APP = {
    .id           = "galaga",
    .display_name = "GALAGA",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_galaga(void) { return &_APP; }
