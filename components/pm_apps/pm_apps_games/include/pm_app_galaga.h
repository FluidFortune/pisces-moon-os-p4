// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_galaga.h — Galaga
//
//  Vertical shooter. Player at the bottom, enemies arrive in
//  formation at the top, dive in waves. Non-blocking explosion
//  queue (the v1.1.0 fix that eliminated freeze on enemy death
//  on S3 carries over).
// ============================================================

#ifndef PM_APP_GALAGA_H
#define PM_APP_GALAGA_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_galaga(void);
void pm_app_galaga_dir(int dx);
void pm_app_galaga_fire(void);
#ifdef __cplusplus
}
#endif
#endif
