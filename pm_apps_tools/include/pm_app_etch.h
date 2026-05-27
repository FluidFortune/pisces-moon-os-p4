// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_etch.h — Etch-A-Sketch drawing canvas
//
//  Touch (and optional gamepad / d-pad) drawing on an LVGL
//  canvas. P4 has multi-touch up to 5 points — a future
//  extension can support multi-finger color/stroke control,
//  but Phase-3 spec keeps it to single-finger black on grey.
//
//  Buttons:
//    CLEAR — wipe canvas
//    COLOR — cycle stroke color
//    SAVE  — write canvas to /sd/etch/<timestamp>.bmp
//            (BMP write under SPI Treaty)
// ============================================================

#ifndef PM_APP_ETCH_H
#define PM_APP_ETCH_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_etch(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_ETCH_H
