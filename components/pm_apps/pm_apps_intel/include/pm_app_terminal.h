// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_terminal.h — Gemini AI terminal
//
//  Conversational AI accessed via Google's Gemini API.
//  Reads GEMINI_API_KEY from secrets.h. Maintains a rolling
//  20-message context window. Saves each session to NoSQL
//  category "gemini_log".
//
//  P4 connectivity model:
//    - Internet is via WiFi (radio on the C6).
//    - C6 firmware exposes a "wifi_connect" + "http_request"
//      command path (Phase-2 stub here, full impl in C6 fw).
//    - Until C6 HTTP is online, this app shows a "no
//      connectivity" notice but lets the user compose prompts.
// ============================================================

#ifndef PM_APP_TERMINAL_H
#define PM_APP_TERMINAL_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_terminal(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_TERMINAL_H
