// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_chess.h — Chess
//
//  Full chess implementation. Move generation + AI from S3
//  ported to ESP-IDF. AI is alpha-beta minimax with simple
//  material evaluation, depth 3 by default.
//
//  Move input methods (multi-input fix from v1.1.0):
//    - Touch: tap source then destination
//    - Algebraic notation: e.g. "Nf3" via on-screen keyboard
//    - Click events from gamepad (as cursor)
// ============================================================

#ifndef PM_APP_CHESS_H
#define PM_APP_CHESS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_chess(void);
void pm_app_chess_tap(int file, int rank);   // 0..7
void pm_app_chess_undo(void);
void pm_app_chess_new_game(void);
#ifdef __cplusplus
}
#endif
#endif
