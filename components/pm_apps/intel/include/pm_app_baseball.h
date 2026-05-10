// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_baseball.h — MLB player card database
//
//  Same browser pattern as ref_med / ref_surv but with the
//  fetch affordance enabled — when an entry isn't local,
//  the user can request fetch via Gemini (queued through
//  C6 HTTP transport, same as terminal).
//
//  Long-term: replaced by Phantom MLB Stats API endpoint
//  over Tailscale, eliminating Gemini dependency. P4 app
//  remains the same — only the fetch backend swaps.
// ============================================================

#ifndef PM_APP_BASEBALL_H
#define PM_APP_BASEBALL_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_baseball(void);
#ifdef __cplusplus
}
#endif
#endif
