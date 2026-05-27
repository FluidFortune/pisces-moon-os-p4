// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_pacman.h — Pac-Man
//
//  Faithful arcade clone. Maze data, ghost AI personalities
//  (Blinky/Pinky/Inky/Clyde), frightened mode, energizer logic
//  carried over from S3 implementation.
//
//  P4 layout: 16-pixel tiles vs S3's 8-pixel — same 28×31
//  maze, twice as crisp on the larger screen.
// ============================================================

#ifndef PM_APP_PACMAN_H
#define PM_APP_PACMAN_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_pacman(void);
void pm_app_pacman_dir(int dx, int dy);
#ifdef __cplusplus
}
#endif
#endif
