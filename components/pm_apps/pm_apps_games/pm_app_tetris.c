// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_tetris.c — Tetris, real port
//
//  Phase 19. Replaces the launcher-stub with a playable game
//  using the same canvas all the other CYBER/COMMS apps use.
//  This is the proof-of-concept that the new layout system
//  ports the game suite cleanly — once tetris feels right,
//  every other game in the GAMES category follows the same
//  shape.
//
//  ── Board model ──
//
//  10 × 20 cell grid. The board is a uint8_t[200] where each
//  entry is 0 (empty) or 1..7 (locked piece colour ID). The
//  falling piece lives separately in a 4×4 mask plus an (x,y)
//  origin and rotation index.
//
//  ── Rendering ──
//
//  Each of the 200 board cells is a single lv_obj. We don't
//  recreate widgets per frame — the cell list is allocated
//  once and recoloured in-place from the board + falling-piece
//  mask. That keeps LVGL invalidation small and the frame
//  rate consistent on the P4.
//
//  ── Input ──
//
//  Five touch zones across the bottom action bar:
//    ◀  ROT  ▶  ▼  ⬇
//  Left / right shift, rotate clockwise, soft drop (one row),
//  hard drop (instant slam). Game over shows a RESTART action
//  in the same bar.
//
//  ── Tetromino tables ──
//
//  Each piece has 4 rotation states stored as four (x,y) cell
//  offsets from the pivot. SRS-style rotation system without
//  wall kicks (sufficient for casual play; competitive
//  rotations can come later with the same data shape).
// ============================================================

#include "pm_app_tetris.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "PM_TETRIS";

#define BOARD_W 10
#define BOARD_H 20
#define CELL_COUNT (BOARD_W * BOARD_H)
#define NUM_PIECES 7
#define MS_PER_LEVEL_REDUCTION 75

// Cell colour palette by piece id (id 0 = empty so we skip it).
static const uint32_t PIECE_COLORS[NUM_PIECES + 1] = {
    0x000000,  // 0 empty (unused — we set bg directly)
    0x4dd9ff,  // 1 I — cyan
    0xffd166,  // 2 O — gold
    0xc89eff,  // 3 T — purple
    0x4dffa6,  // 4 S — green
    0xff5577,  // 5 Z — red
    0x4d9eff,  // 6 J — blue
    0xffa040,  // 7 L — orange
};

// Tetromino cell offsets — 4 cells per piece, 4 rotation states.
// Each entry is {dx, dy} relative to piece origin.
typedef struct { int8_t dx, dy; } cell_off_t;
static const cell_off_t PIECES[NUM_PIECES][4][4] = {
    // I — straight bar
    { { {0,1},{1,1},{2,1},{3,1} },
      { {2,0},{2,1},{2,2},{2,3} },
      { {0,2},{1,2},{2,2},{3,2} },
      { {1,0},{1,1},{1,2},{1,3} } },
    // O — square (rotation is identity)
    { { {1,0},{2,0},{1,1},{2,1} },
      { {1,0},{2,0},{1,1},{2,1} },
      { {1,0},{2,0},{1,1},{2,1} },
      { {1,0},{2,0},{1,1},{2,1} } },
    // T
    { { {0,1},{1,1},{2,1},{1,2} },
      { {1,0},{1,1},{2,1},{1,2} },
      { {0,1},{1,1},{2,1},{1,0} },
      { {1,0},{0,1},{1,1},{1,2} } },
    // S
    { { {1,1},{2,1},{0,2},{1,2} },
      { {1,0},{1,1},{2,1},{2,2} },
      { {1,1},{2,1},{0,2},{1,2} },
      { {1,0},{1,1},{2,1},{2,2} } },
    // Z
    { { {0,1},{1,1},{1,2},{2,2} },
      { {2,0},{1,1},{2,1},{1,2} },
      { {0,1},{1,1},{1,2},{2,2} },
      { {2,0},{1,1},{2,1},{1,2} } },
    // J
    { { {0,1},{1,1},{2,1},{2,2} },
      { {1,0},{1,1},{1,2},{2,0} },
      { {0,0},{0,1},{1,1},{2,1} },
      { {1,0},{1,1},{1,2},{0,2} } },
    // L
    { { {0,1},{1,1},{2,1},{0,2} },
      { {1,0},{1,1},{1,2},{2,2} },
      { {0,1},{1,1},{2,1},{2,0} },
      { {0,0},{1,0},{1,1},{1,2} } },
};

// ── State ──────────────────────────────────────────────────
static uint8_t s_board[CELL_COUNT];

static int s_cur_piece;
static int s_cur_rot;
static int s_cur_x;
static int s_cur_y;
static int s_next_piece;

static int s_score;
static int s_lines;
static int s_level;
static bool s_game_over;
static bool s_paused;
static uint32_t s_gravity_accum_ms;
static uint32_t s_last_tick_ms;

// ── UI ─────────────────────────────────────────────────────
static lv_obj_t* s_screen          = NULL;
static lv_obj_t* s_board_widget    = NULL;
static lv_obj_t* s_cells[CELL_COUNT];
static lv_obj_t* s_next_widget     = NULL;
static lv_obj_t* s_next_cells[16];
static lv_obj_t* s_stat_score      = NULL;
static lv_obj_t* s_stat_lines      = NULL;
static lv_obj_t* s_stat_level      = NULL;
static lv_obj_t* s_chip_state      = NULL;
static lv_obj_t* s_message_lbl     = NULL;
static bool      s_built           = false;
static int       s_cell_px         = 22;

// ── Helpers ────────────────────────────────────────────────
static inline int _idx(int x, int y) { return y * BOARD_W + x; }

static bool _cell_filled(int x, int y) {
    if (x < 0 || x >= BOARD_W || y < 0 || y >= BOARD_H) return true;
    return s_board[_idx(x, y)] != 0;
}

static bool _piece_fits(int piece, int rot, int ox, int oy) {
    for (int i = 0; i < 4; i++) {
        int cx = ox + PIECES[piece][rot][i].dx;
        int cy = oy + PIECES[piece][rot][i].dy;
        if (_cell_filled(cx, cy)) return false;
    }
    return true;
}

static int _gravity_period_ms(int level) {
    // Standard NES-ish curve: ~500 ms at level 1 down to 100 ms by 10.
    int p = 600 - level * MS_PER_LEVEL_REDUCTION;
    if (p < 80) p = 80;
    return p;
}

// ── Render ─────────────────────────────────────────────────
static void _color_cell(lv_obj_t* obj, uint8_t id) {
    if (!obj) return;
    if (id == 0) {
        lv_obj_set_style_bg_color(obj, PM_LAYOUT_COL_BG3, 0);
        lv_obj_set_style_border_color(obj, PM_LAYOUT_COL_BORDER, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
    } else {
        lv_obj_set_style_bg_color(obj, lv_color_hex(PIECE_COLORS[id]), 0);
        lv_obj_set_style_border_color(obj, lv_color_white(), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
    }
}

static void _render_board(void) {
    if (!s_screen) return;
    // Start with board state.
    for (int i = 0; i < CELL_COUNT; i++) {
        _color_cell(s_cells[i], s_board[i]);
    }
    // Overlay active piece.
    if (!s_game_over) {
        uint8_t id = (uint8_t)(s_cur_piece + 1);
        for (int i = 0; i < 4; i++) {
            int cx = s_cur_x + PIECES[s_cur_piece][s_cur_rot][i].dx;
            int cy = s_cur_y + PIECES[s_cur_piece][s_cur_rot][i].dy;
            if (cx >= 0 && cx < BOARD_W && cy >= 0 && cy < BOARD_H) {
                _color_cell(s_cells[_idx(cx, cy)], id);
            }
        }
    }
    // Stats
    if (s_stat_score) {
        char b[16]; snprintf(b, sizeof(b), "%d", s_score);
        lv_label_set_text(s_stat_score, b);
    }
    if (s_stat_lines) {
        char b[16]; snprintf(b, sizeof(b), "%d", s_lines);
        lv_label_set_text(s_stat_lines, b);
    }
    if (s_stat_level) {
        char b[16]; snprintf(b, sizeof(b), "%d", s_level);
        lv_label_set_text(s_stat_level, b);
    }
    if (s_chip_state) {
        if (s_game_over) {
            lv_label_set_text(s_chip_state, "GAME OVER");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_ERR, 0);
        } else if (s_paused) {
            lv_label_set_text(s_chip_state, "PAUSED");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_WARN, 0);
        } else {
            lv_label_set_text(s_chip_state, "PLAYING");
            lv_obj_set_style_text_color(s_chip_state, PM_LAYOUT_COL_OK, 0);
        }
    }
    // Next-piece preview
    if (s_next_widget) {
        for (int i = 0; i < 16; i++) {
            _color_cell(s_next_cells[i], 0);
        }
        uint8_t nid = (uint8_t)(s_next_piece + 1);
        for (int i = 0; i < 4; i++) {
            int cx = PIECES[s_next_piece][0][i].dx;
            int cy = PIECES[s_next_piece][0][i].dy;
            if (cx >= 0 && cx < 4 && cy >= 0 && cy < 4) {
                _color_cell(s_next_cells[cy * 4 + cx], nid);
            }
        }
    }
    if (s_message_lbl) {
        lv_label_set_text(s_message_lbl,
            s_game_over ? "Tap RESTART to play again" :
            s_paused    ? "Paused — tap PAUSE to resume" :
                          "Clear lines for points. Speed up as you level.");
    }
}

// ── Game mechanics ─────────────────────────────────────────
static void _spawn_piece(void) {
    s_cur_piece = s_next_piece;
    s_next_piece = rand() % NUM_PIECES;
    s_cur_rot = 0;
    s_cur_x = (BOARD_W / 2) - 2;
    s_cur_y = 0;
    if (!_piece_fits(s_cur_piece, s_cur_rot, s_cur_x, s_cur_y)) {
        s_game_over = true;
    }
}

static void _lock_piece(void) {
    uint8_t id = (uint8_t)(s_cur_piece + 1);
    for (int i = 0; i < 4; i++) {
        int cx = s_cur_x + PIECES[s_cur_piece][s_cur_rot][i].dx;
        int cy = s_cur_y + PIECES[s_cur_piece][s_cur_rot][i].dy;
        if (cx >= 0 && cx < BOARD_W && cy >= 0 && cy < BOARD_H) {
            s_board[_idx(cx, cy)] = id;
        }
    }
}

static int _clear_lines(void) {
    int cleared = 0;
    for (int y = BOARD_H - 1; y >= 0; ) {
        bool full = true;
        for (int x = 0; x < BOARD_W; x++) {
            if (s_board[_idx(x, y)] == 0) { full = false; break; }
        }
        if (full) {
            // Shift everything above down.
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_W; x++) {
                    s_board[_idx(x, yy)] = s_board[_idx(x, yy - 1)];
                }
            }
            for (int x = 0; x < BOARD_W; x++) s_board[_idx(x, 0)] = 0;
            cleared++;
            // Don't decrement y — re-check this row after the shift.
        } else {
            y--;
        }
    }
    return cleared;
}

// Standard line-clear score table (1=100, 2=300, 3=500, 4=800), × level.
static const int LINE_SCORES[5] = {0, 100, 300, 500, 800};

static void _apply_clear(int n) {
    if (n <= 0 || n > 4) return;
    s_score += LINE_SCORES[n] * s_level;
    s_lines += n;
    int new_level = 1 + s_lines / 10;
    if (new_level > s_level) s_level = new_level;
}

static void _try_move(int dx, int dy) {
    if (s_game_over || s_paused) return;
    if (_piece_fits(s_cur_piece, s_cur_rot, s_cur_x + dx, s_cur_y + dy)) {
        s_cur_x += dx;
        s_cur_y += dy;
    }
}

static void _try_rotate(void) {
    if (s_game_over || s_paused) return;
    int next_rot = (s_cur_rot + 1) & 3;
    if (_piece_fits(s_cur_piece, next_rot, s_cur_x, s_cur_y)) {
        s_cur_rot = next_rot;
    }
}

static void _hard_drop(void) {
    if (s_game_over || s_paused) return;
    while (_piece_fits(s_cur_piece, s_cur_rot, s_cur_x, s_cur_y + 1)) {
        s_cur_y++;
        s_score += 2;   // +2 per row dropped
    }
    _lock_piece();
    int n = _clear_lines();
    _apply_clear(n);
    _spawn_piece();
}

static void _gravity_step(void) {
    if (s_game_over || s_paused) return;
    if (_piece_fits(s_cur_piece, s_cur_rot, s_cur_x, s_cur_y + 1)) {
        s_cur_y++;
    } else {
        _lock_piece();
        int n = _clear_lines();
        _apply_clear(n);
        _spawn_piece();
    }
}

static void _reset_game(void) {
    memset(s_board, 0, sizeof(s_board));
    s_score = 0;
    s_lines = 0;
    s_level = 1;
    s_game_over = false;
    s_paused = false;
    s_gravity_accum_ms = 0;
    s_next_piece = rand() % NUM_PIECES;
    _spawn_piece();
}

// ── Action callbacks ───────────────────────────────────────
static void _ev_left(lv_event_t* e)    { (void)e; _try_move(-1, 0); _render_board(); }
static void _ev_right(lv_event_t* e)   { (void)e; _try_move( 1, 0); _render_board(); }
static void _ev_rotate(lv_event_t* e)  { (void)e; _try_rotate();   _render_board(); }
static void _ev_soft(lv_event_t* e)    { (void)e; _try_move(0, 1); s_score += 1; _render_board(); }
static void _ev_hard(lv_event_t* e)    { (void)e; _hard_drop(); _render_board(); }
static void _ev_pause(lv_event_t* e)   {
    (void)e;
    if (s_game_over) { _reset_game(); }
    else s_paused = !s_paused;
    _render_board();
}

// ── Screen build ───────────────────────────────────────────
static void _build_screen(void) {
    if (s_built) return;

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "TETRIS");

    s_chip_state = pm_app_layout_chip(&L, "PLAYING", PM_LAYOUT_COL_OK);

    pm_app_layout_stats_row(&L, 3);
    s_stat_score = pm_app_layout_stat(&L, "SCORE", "0");
    s_stat_lines = pm_app_layout_stat(&L, "LINES", "0");
    s_stat_level = pm_app_layout_stat(&L, "LEVEL", "1");

    pm_app_layout_content(&L);

#if PM_BOARD_LCD_H_RES <= 800
    s_cell_px = 20;
    int side_w = 220;
#else
    s_cell_px = 26;
    int side_w = 280;
#endif

    // CENTER pane: the board (with side margins so it's centered)
    lv_obj_t* center = pm_app_layout_pane(&L, 0, NULL);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int board_w = BOARD_W * s_cell_px + 4;
    int board_h = BOARD_H * s_cell_px + 4;
    s_board_widget = lv_obj_create(center);
    lv_obj_remove_style_all(s_board_widget);
    lv_obj_set_size(s_board_widget, board_w, board_h);
    lv_obj_set_style_bg_color(s_board_widget, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_board_widget, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_board_widget, PM_LAYOUT_COL_ACCENT, 0);
    lv_obj_set_style_border_width(s_board_widget, 2, 0);
    lv_obj_set_style_pad_all(s_board_widget, 2, 0);
    lv_obj_set_layout(s_board_widget, LV_LAYOUT_GRID);
    lv_obj_clear_flag(s_board_widget, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[BOARD_W + 1];
    static lv_coord_t row_dsc[BOARD_H + 1];
    for (int i = 0; i < BOARD_W; i++) col_dsc[i] = s_cell_px;
    col_dsc[BOARD_W] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < BOARD_H; i++) row_dsc[i] = s_cell_px;
    row_dsc[BOARD_H] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_grid_column_dsc_array(s_board_widget, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(s_board_widget, row_dsc, 0);
    lv_obj_set_style_pad_gap(s_board_widget, 0, 0);

    for (int y = 0; y < BOARD_H; y++) {
        for (int x = 0; x < BOARD_W; x++) {
            lv_obj_t* c = lv_obj_create(s_board_widget);
            lv_obj_remove_style_all(c);
            lv_obj_set_grid_cell(c, LV_GRID_ALIGN_STRETCH, x, 1,
                                     LV_GRID_ALIGN_STRETCH, y, 1);
            lv_obj_set_style_bg_color(c, PM_LAYOUT_COL_BG3, 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(c, PM_LAYOUT_COL_BORDER, 0);
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            s_cells[_idx(x, y)] = c;
        }
    }

    // RIGHT pane: next-piece preview + message
    lv_obj_t* right = pm_app_layout_pane(&L, side_w, NULL);
    pm_app_layout_section_header(right, "NEXT", NULL);

    int prev_cell = s_cell_px - 2;
    s_next_widget = lv_obj_create(right);
    lv_obj_remove_style_all(s_next_widget);
    lv_obj_set_size(s_next_widget,
                     4 * prev_cell + 4, 4 * prev_cell + 4);
    lv_obj_set_style_bg_color(s_next_widget, PM_LAYOUT_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_next_widget, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_next_widget, PM_LAYOUT_COL_BORDER, 0);
    lv_obj_set_style_border_width(s_next_widget, 1, 0);
    lv_obj_set_style_pad_all(s_next_widget, 2, 0);
    lv_obj_set_style_margin_all(s_next_widget, 12, 0);
    lv_obj_set_layout(s_next_widget, LV_LAYOUT_GRID);

    static lv_coord_t prev_col[5];
    static lv_coord_t prev_row[5];
    for (int i = 0; i < 4; i++) { prev_col[i] = prev_cell; prev_row[i] = prev_cell; }
    prev_col[4] = LV_GRID_TEMPLATE_LAST;
    prev_row[4] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_grid_column_dsc_array(s_next_widget, prev_col, 0);
    lv_obj_set_style_grid_row_dsc_array(s_next_widget, prev_row, 0);
    lv_obj_set_style_pad_gap(s_next_widget, 0, 0);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            lv_obj_t* c = lv_obj_create(s_next_widget);
            lv_obj_remove_style_all(c);
            lv_obj_set_grid_cell(c, LV_GRID_ALIGN_STRETCH, x, 1,
                                     LV_GRID_ALIGN_STRETCH, y, 1);
            lv_obj_set_style_bg_color(c, PM_LAYOUT_COL_BG3, 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(c, PM_LAYOUT_COL_BORDER, 0);
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            s_next_cells[y * 4 + x] = c;
        }
    }

    pm_app_layout_section_header(right, "STATUS", NULL);
    s_message_lbl = lv_label_create(right);
    lv_label_set_text(s_message_lbl, "Clear lines for points.");
    lv_label_set_long_mode(s_message_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_message_lbl, LV_PCT(100));
    lv_obj_set_style_pad_all(s_message_lbl, 14, 0);
    lv_obj_set_style_text_font(s_message_lbl, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_message_lbl, PM_LAYOUT_COL_FG_DIM, 0);

    // Action bar — touch controls
    pm_app_layout_action(&L, "◀ LEFT",   PM_LAYOUT_COL_ACCENT, _ev_left);
    pm_app_layout_action(&L, "ROTATE",   PM_LAYOUT_COL_GOLD,   _ev_rotate);
    pm_app_layout_action(&L, "RIGHT ▶",  PM_LAYOUT_COL_ACCENT, _ev_right);
    pm_app_layout_action(&L, "SOFT ▼",   PM_LAYOUT_COL_OK,     _ev_soft);
    pm_app_layout_action(&L, "HARD ⬇",   PM_LAYOUT_COL_ERR,    _ev_hard);
    pm_app_layout_action(&L, "PAUSE",    PM_LAYOUT_COL_PURPLE, _ev_pause);

    s_screen = pm_app_layout_end(&L);
    s_built = true;
}

// ── Lifecycle ──────────────────────────────────────────────
static void _init(void) {
    if (!s_built) {
        _reset_game();
        _build_screen();
    }
}

static void _enter(void) {
    if (!s_built) _init();
    if (s_screen) lv_screen_load(s_screen);
    s_last_tick_ms = pm_millis();
    _render_board();
    pm_log_i(TAG, "enter");
}

static void _tick(uint32_t e) {
    (void)e;
    if (s_game_over || s_paused) return;
    uint32_t now = pm_millis();
    uint32_t dt  = now - s_last_tick_ms;
    s_last_tick_ms = now;
    s_gravity_accum_ms += dt;
    int period = _gravity_period_ms(s_level);
    if (s_gravity_accum_ms >= (uint32_t)period) {
        s_gravity_accum_ms = 0;
        _gravity_step();
        _render_board();
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
}

static const pm_app_t _APP = {
    .id           = "tetris",
    .display_name = "TETRIS",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_tetris(void) { return &_APP; }
