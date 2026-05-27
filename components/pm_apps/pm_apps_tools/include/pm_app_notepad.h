// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_notepad.h — Plain-text note editor
//
//  Persistence: /sd/logs/<filename>.txt
//  Save model: header tap → "Save As" prompt → write under
//  SPI Treaty mutex.
//  Buffer:     PSRAM-backed, 64KB cap.
//
//  P4 advantage over S3: with 1024×600 we get a much larger
//  edit area. Default layout is 80 cols × 35 rows visible
//  with on-screen soft keyboard hidden by default (touch
//  the body to reveal).
// ============================================================

#ifndef PM_APP_NOTEPAD_H
#define PM_APP_NOTEPAD_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_notepad(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_NOTEPAD_H
