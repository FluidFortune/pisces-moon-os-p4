// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_snake.h — Snake
//
//  Logic-port of S3 snake.cpp to ESP-IDF C with LVGL canvas
//  rendering. High score persisted via pm_nosql category
//  "highscores", id "snake".
//
//  Layout on P4 (1024×600 vs S3 320×240):
//    Cell size     : 16 px (was 6 px)
//    Grid          : 60 cols × 32 rows (was ~50×30)
//    Frame budget  : 60 fps target — game logic ticked
//                     at fixed 12 Hz (every 5 frames).
// ============================================================

#ifndef PM_APP_SNAKE_H
#define PM_APP_SNAKE_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_snake(void);
void pm_app_snake_dir(int dx, int dy);
void pm_app_snake_restart(void);
#ifdef __cplusplus
}
#endif
#endif
