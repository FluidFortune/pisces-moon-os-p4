// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_gemini_log.h — Saved Gemini conversation browser
//
//  Lists session files from NoSQL category "gemini_log".
//  Selecting a session shows the saved conversation and
//  offers Resume / Delete / Export.
// ============================================================

#ifndef PM_APP_GEMINI_LOG_H
#define PM_APP_GEMINI_LOG_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_gemini_log(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_GEMINI_LOG_H
