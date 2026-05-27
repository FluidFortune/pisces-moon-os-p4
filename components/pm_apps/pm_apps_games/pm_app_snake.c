// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_snake.c — Snake
//
//  Game state runs in tick(); rendering is via LVGL canvas
//  draw operations (stubbed). Cell-based grid, ring-buffer
//  body representation.
// ============================================================

#include "pm_app_snake.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include "pm_cardputer_i2c.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_SNAKE";

#define COLS         60
#define ROWS         32
#define MAX_LEN      (COLS * ROWS)
#define TICK_INTERVAL_MS  85   // ~12 Hz

#ifndef PM_SNAKE_ENABLE_TOUCH_GAMEPAD
#define PM_SNAKE_ENABLE_TOUCH_GAMEPAD 1
#endif

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir_t;

typedef struct { int8_t col, row; } pt_t;

typedef enum { ST_RUN, ST_DEAD } state_t;

static pt_t    s_body[MAX_LEN];
static int     s_len      = 3;
static int     s_score    = 0;
static int     s_high     = 0;
static dir_t   s_dir      = DIR_RIGHT;
static dir_t   s_next_dir = DIR_RIGHT;
static state_t s_state    = ST_RUN;
static int     s_food_col = 10;
static int     s_food_row = 10;
static uint32_t s_last_tick = 0;

// ─────────────────────────────────────────────
//  Persistence
// ─────────────────────────────────────────────
static int _load_highscore(void) {
    char buf[16];
    size_t got = pm_nosql_read("highscores", "snake", buf, sizeof(buf));
    if (got == 0) return 0;
    return atoi(buf);
}

static void _save_highscore(int v) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", v);
    pm_nosql_write("highscores", "snake", buf, (size_t)n);
}

// ─────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────
static void _place_food(void) {
    while (1) {
        int c = pm_random_range(1, COLS - 2);
        int r = pm_random_range(1, ROWS - 2);
        // Avoid overlap with body
        bool clash = false;
        for (int i = 0; i < s_len; i++)
            if (s_body[i].col == c && s_body[i].row == r) { clash = true; break; }
        if (!clash) { s_food_col = c; s_food_row = r; return; }
    }
}

static void _reset_game(void) {
    s_len      = 3;
    s_score    = 0;
    s_dir      = DIR_RIGHT;
    s_next_dir = DIR_RIGHT;
    s_state    = ST_RUN;
    for (int i = 0; i < s_len; i++) {
        s_body[i].col = COLS / 2 - i;
        s_body[i].row = ROWS / 2;
    }
    _place_food();
}

void pm_app_snake_restart(void) { _reset_game(); }

void pm_app_snake_dir(int dx, int dy) {
    dir_t want = s_dir;
    if      (dx ==  1 && s_dir != DIR_LEFT)  want = DIR_RIGHT;
    else if (dx == -1 && s_dir != DIR_RIGHT) want = DIR_LEFT;
    else if (dy ==  1 && s_dir != DIR_UP)    want = DIR_DOWN;
    else if (dy == -1 && s_dir != DIR_DOWN)  want = DIR_UP;
    s_next_dir = want;
}

static void _step(void) {
    if (s_state != ST_RUN) return;
    s_dir = s_next_dir;

    int dx = 0, dy = 0;
    switch (s_dir) {
        case DIR_UP:    dy = -1; break;
        case DIR_DOWN:  dy =  1; break;
        case DIR_LEFT:  dx = -1; break;
        case DIR_RIGHT: dx =  1; break;
    }

    int new_col = s_body[0].col + dx;
    int new_row = s_body[0].row + dy;

    // Wall collision
    if (new_col < 1 || new_col >= COLS - 1 ||
        new_row < 1 || new_row >= ROWS - 1) {
        s_state = ST_DEAD;
        if (s_score > s_high) { s_high = s_score; _save_highscore(s_high); }
        return;
    }

    // Self collision (skip tail — it'll be moving)
    for (int i = 0; i < s_len - 1; i++) {
        if (s_body[i].col == new_col && s_body[i].row == new_row) {
            s_state = ST_DEAD;
            if (s_score > s_high) { s_high = s_score; _save_highscore(s_high); }
            return;
        }
    }

    // Did we eat?
    bool ate = (new_col == s_food_col && new_row == s_food_row);
    if (ate) {
        s_score += 10;
        if (s_len < MAX_LEN) s_len++;
    }

    // Shift body
    for (int i = s_len - 1; i > 0; i--) s_body[i] = s_body[i - 1];
    s_body[0].col = new_col;
    s_body[0].row = new_row;

    if (ate) _place_food();
}

// ─────────────────────────────────────────────
//  Render (LVGL canvas — stubbed)
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: draw black background, grid border, snake cells
    //            (head bright, body dim), food cell, score header.
    pm_log_d(TAG, "score=%d len=%d state=%d", s_score, s_len, (int)s_state);
}

// ─────────────────────────────────────────────
//  Lifecycle — Phase 14: pm_input + on-screen gamepad
// ─────────────────────────────────────────────
#include "pm_input.h"

static lv_obj_t*        s_default_screen = NULL;
static pm_ui_gamepad_t* s_gp_overlay     = NULL;
static int              s_sub_token      = -1;

static bool _touch_gamepad_needed(void) {
#if PM_SNAKE_ENABLE_TOUCH_GAMEPAD
    return !pm_cardputer_i2c_link_seen();
#else
    return false;
#endif
}

static void _on_input(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e || !e->down) return;
    if (e->kind == PM_INPUT_DPAD) {
        if      (e->code & PM_DPAD_LEFT)  pm_app_snake_dir(-1, 0);
        else if (e->code & PM_DPAD_RIGHT) pm_app_snake_dir( 1, 0);
        else if (e->code & PM_DPAD_UP)    pm_app_snake_dir( 0,-1);
        else if (e->code & PM_DPAD_DOWN)  pm_app_snake_dir( 0, 1);
    }
    else if (e->kind == PM_INPUT_KEY) {
        switch (e->code) {
            case PM_KEY_LEFT:  pm_app_snake_dir(-1, 0); break;
            case PM_KEY_RIGHT: pm_app_snake_dir( 1, 0); break;
            case PM_KEY_UP:    pm_app_snake_dir( 0,-1); break;
            case PM_KEY_DOWN:  pm_app_snake_dir( 0, 1); break;
        }
    }
    else if (e->kind == PM_INPUT_BUTTON) {
        if (e->code == PM_BTN_START && s_state == ST_DEAD) _reset_game();
    }
}

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("SNAKE",
        "SNAKE ready");
#if PM_SNAKE_ENABLE_TOUCH_GAMEPAD
    if (_touch_gamepad_needed()) {
        s_gp_overlay = pm_ui_gamepad_create(s_default_screen);
    }
#endif
}

static void _init(void)  { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    s_high = _load_highscore();
    _reset_game();
    s_last_tick = pm_millis();
    _render();
    if (_touch_gamepad_needed()) {
        if (!s_gp_overlay && s_default_screen) {
            s_gp_overlay = pm_ui_gamepad_create(s_default_screen);
        }
        if (s_gp_overlay) pm_ui_gamepad_show(s_gp_overlay);
    } else if (s_gp_overlay) {
        pm_ui_gamepad_hide(s_gp_overlay);
    }
    if (s_sub_token < 0) s_sub_token = pm_input_subscribe(_on_input, NULL);
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    uint32_t now = pm_millis();
    if (now - s_last_tick < TICK_INTERVAL_MS) return;
    s_last_tick = now;
    _step();
    _render();
}

static void _exit_(void) {
    pm_log_i(TAG, "exit (score=%d)", s_score);
    if (s_sub_token >= 0) { pm_input_unsubscribe(s_sub_token); s_sub_token = -1; }
    if (s_gp_overlay) pm_ui_gamepad_hide(s_gp_overlay);
}

static const pm_app_t _APP = {
    .id           = "snake",
    .display_name = "SNAKE",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_snake(void) { return &_APP; }
