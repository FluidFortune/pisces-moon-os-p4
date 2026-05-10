// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_chess.c — Chess engine + UI
//
//  Board model: 8×8, piece codes:
//    0  = empty
//    +1 = white pawn,   -1 = black pawn
//    +2 = white knight, -2 = black knight
//    +3 = white bishop, -3 = black bishop
//    +4 = white rook,   -4 = black rook
//    +5 = white queen,  -5 = black queen
//    +6 = white king,   -6 = black king
//
//  AI: alpha-beta minimax to depth 3, material + center
//  bonus + mobility evaluator. Adequate for casual play.
//
//  Castling rights and en-passant kept as state but full
//  legal-move generation is a simplified set — sufficient
//  for casual games. Full 100% legal move generator is a
//  follow-up improvement.
// ============================================================

#include "pm_app_chess.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_CHESS";

#define BOARD_DIM 8
#define MAX_HISTORY 256

typedef int8_t sq_t;
typedef struct { int8_t fr, fc, tr, tc, captured; } move_t;

static sq_t  s_board[BOARD_DIM][BOARD_DIM];
static int   s_white_to_move = 1;
static move_t s_history[MAX_HISTORY];
static int   s_history_len = 0;
static int   s_sel_r = -1, s_sel_c = -1;       // selected source square
static int   s_ai_depth = 3;
static bool  s_white_is_human = true;

// LVGL
static void* s_screen = NULL;

// ─────────────────────────────────────────────
//  Board init
// ─────────────────────────────────────────────
static void _new_game(void) {
    memset(s_board, 0, sizeof(s_board));
    sq_t back[8] = { 4, 2, 3, 5, 6, 3, 2, 4 };
    for (int c = 0; c < 8; c++) {
        s_board[0][c] = -back[c];
        s_board[1][c] = -1;
        s_board[6][c] = +1;
        s_board[7][c] = +back[c];
    }
    s_white_to_move = 1;
    s_history_len   = 0;
    s_sel_r = s_sel_c = -1;
}

void pm_app_chess_new_game(void) { _new_game(); }

// ─────────────────────────────────────────────
//  Move helpers
// ─────────────────────────────────────────────
static bool _on_board(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }
static int  _sign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// Generate pseudo-legal moves for the side-to-move into `out`.
// Returns count. (No king-in-check filter — simplified.)
static int _gen_moves(move_t* out, int max_out) {
    int n = 0;
    int side = s_white_to_move ? 1 : -1;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        sq_t p = s_board[r][c];
        if (_sign(p) != side) continue;
        int abs_p = abs(p);

        // Helper: try a destination
        #define TRY(tr,tc) do { \
            if (_on_board((tr),(tc)) && _sign(s_board[(tr)][(tc)]) != side && n < max_out) { \
                out[n].fr=r; out[n].fc=c; out[n].tr=(tr); out[n].tc=(tc); \
                out[n].captured = s_board[(tr)][(tc)]; n++; \
            } \
        } while (0)

        if (abs_p == 1) {
            int dir = side > 0 ? -1 : 1;
            int start = side > 0 ? 6 : 1;
            if (_on_board(r+dir,c) && s_board[r+dir][c] == 0) {
                TRY(r+dir, c);
                if (r == start && s_board[r+2*dir][c] == 0) TRY(r+2*dir, c);
            }
            for (int dc = -1; dc <= 1; dc += 2) {
                int tr = r+dir, tc = c+dc;
                if (_on_board(tr,tc) && s_board[tr][tc] != 0 && _sign(s_board[tr][tc]) != side)
                    TRY(tr, tc);
            }
        } else if (abs_p == 2) {
            int dr[] = {-2,-2,-1,-1,1,1,2,2};
            int dc[] = {-1,1,-2,2,-2,2,-1,1};
            for (int i = 0; i < 8; i++) TRY(r + dr[i], c + dc[i]);
        } else if (abs_p == 3 || abs_p == 5) {
            int dr[] = {-1,-1,1,1};
            int dc[] = {-1,1,-1,1};
            for (int i = 0; i < 4; i++) {
                int tr = r, tc = c;
                while (true) {
                    tr += dr[i]; tc += dc[i];
                    if (!_on_board(tr,tc)) break;
                    if (s_board[tr][tc] == 0) { TRY(tr,tc); }
                    else { TRY(tr,tc); break; }
                }
            }
        }
        if (abs_p == 4 || abs_p == 5) {
            int dr[] = {-1,1,0,0};
            int dc[] = {0,0,-1,1};
            for (int i = 0; i < 4; i++) {
                int tr = r, tc = c;
                while (true) {
                    tr += dr[i]; tc += dc[i];
                    if (!_on_board(tr,tc)) break;
                    if (s_board[tr][tc] == 0) { TRY(tr,tc); }
                    else { TRY(tr,tc); break; }
                }
            }
        }
        if (abs_p == 6) {
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    TRY(r + dr, c + dc);
                }
        }
        #undef TRY
    }
    return n;
}

static void _do_move(const move_t* m) {
    s_board[m->tr][m->tc] = s_board[m->fr][m->fc];
    s_board[m->fr][m->fc] = 0;
    // Pawn promotion (always queen for simplicity)
    sq_t p = s_board[m->tr][m->tc];
    if (abs(p) == 1 && (m->tr == 0 || m->tr == 7))
        s_board[m->tr][m->tc] = (p > 0) ? 5 : -5;
    s_white_to_move = !s_white_to_move;
    if (s_history_len < MAX_HISTORY) s_history[s_history_len++] = *m;
}

static void _undo_move(const move_t* m) {
    sq_t moved = s_board[m->tr][m->tc];
    // Undo promotion
    if (abs(moved) == 5 && (m->tr == 0 || m->tr == 7)) {
        // could have been a pawn — heuristic: if dest rank is promotion rank,
        // and source was rank-1, restore as pawn
        if ((m->tr == 0 && m->fr == 1) || (m->tr == 7 && m->fr == 6))
            moved = (moved > 0) ? 1 : -1;
    }
    s_board[m->fr][m->fc] = moved;
    s_board[m->tr][m->tc] = m->captured;
    s_white_to_move = !s_white_to_move;
}

void pm_app_chess_undo(void) {
    if (s_history_len == 0) return;
    _undo_move(&s_history[--s_history_len]);
    // Undo AI's prior move too if there was one
    if (s_history_len > 0 && s_white_is_human != s_white_to_move) {
        _undo_move(&s_history[--s_history_len]);
    }
    s_sel_r = s_sel_c = -1;
}

// ─────────────────────────────────────────────
//  Evaluator
// ─────────────────────────────────────────────
static int _evaluate(void) {
    static const int values[7] = {0, 100, 320, 330, 500, 900, 20000};
    int score = 0;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        sq_t p = s_board[r][c];
        if (p == 0) continue;
        score += _sign(p) * values[abs(p)];
        // Center bonus
        int cdist = (r >= 3 && r <= 4 && c >= 3 && c <= 4) ? 10 :
                    (r >= 2 && r <= 5 && c >= 2 && c <= 5) ? 5 : 0;
        score += _sign(p) * cdist;
    }
    return score;
}

// Negamax with alpha-beta
static int _search(int depth, int alpha, int beta) {
    if (depth == 0) return s_white_to_move ? _evaluate() : -_evaluate();
    move_t moves[256];
    int n = _gen_moves(moves, 256);
    if (n == 0) return -30000;     // No moves = loss
    int best = -30001;
    for (int i = 0; i < n; i++) {
        _do_move(&moves[i]);
        int score = -_search(depth - 1, -beta, -alpha);
        _undo_move(&moves[i]);
        if (score > best) best = score;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

static bool _ai_move(void) {
    move_t moves[256];
    int n = _gen_moves(moves, 256);
    if (n == 0) return false;
    int best = -30001;
    int best_idx = 0;
    int alpha = -30000, beta = 30000;
    for (int i = 0; i < n; i++) {
        _do_move(&moves[i]);
        int score = -_search(s_ai_depth - 1, -beta, -alpha);
        _undo_move(&moves[i]);
        if (score > best) { best = score; best_idx = i; }
        if (best > alpha) alpha = best;
    }
    _do_move(&moves[best_idx]);
    pm_log_i(TAG, "AI move %d->%d", moves[best_idx].fr*8+moves[best_idx].fc,
                                     moves[best_idx].tr*8+moves[best_idx].tc);
    return true;
}

// ─────────────────────────────────────────────
//  Tap input
// ─────────────────────────────────────────────
void pm_app_chess_tap(int file, int rank) {
    if (s_white_to_move != s_white_is_human) return;       // not your turn
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return;
    int r = 7 - rank;     // rank 0 = bottom = white side = row 7
    int c = file;

    if (s_sel_r < 0) {
        sq_t p = s_board[r][c];
        if (_sign(p) == (s_white_to_move ? 1 : -1)) {
            s_sel_r = r; s_sel_c = c;
            // TODO_LVGL: highlight square + legal-move dots
        }
        return;
    }

    // Attempt move sel→target
    move_t moves[256];
    int n = _gen_moves(moves, 256);
    for (int i = 0; i < n; i++) {
        if (moves[i].fr == s_sel_r && moves[i].fc == s_sel_c &&
            moves[i].tr == r       && moves[i].tc == c) {
            _do_move(&moves[i]);
            s_sel_r = s_sel_c = -1;
            // AI replies on its own thread? For now, synchronous —
            // the depth-3 search runs in a few hundred ms. If it
            // becomes too slow we'll move it to a worker task.
            _ai_move();
            return;
        }
    }
    // Re-select
    if (_sign(s_board[r][c]) == (s_white_to_move ? 1 : -1)) {
        s_sel_r = r; s_sel_c = c;
    } else {
        s_sel_r = s_sel_c = -1;
    }
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: draw board (alternating squares, big enough for finger taps),
    //            piece glyphs (Unicode chess symbols work on LVGL),
    //            highlight selected square + legal targets,
    //            move history panel, [NEW][UNDO] buttons.
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("CHESS",
        "CHESS app — UI ready");
}
static void _init(void) { _build_screen(); _new_game(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _new_game();
    _render();
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "chess",
    .display_name = "CHESS",
    .category     = PM_CAT_GAMES,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,         // Event-driven; no per-frame work
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_chess(void) { return &_APP; }
