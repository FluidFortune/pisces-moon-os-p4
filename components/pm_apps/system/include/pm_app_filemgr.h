// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_filemgr.h — WiFi file manager
//
//  Brings up an HTTP server on a local AP (or the joined STA)
//  and serves /sd over a basic web UI: list, download, upload.
//
//  On Pisces Moon P4, WiFi for general AP/STA connectivity
//  lives on the C6 alongside Ghost Engine. This app sends
//  a command to the C6 to enter "filemgr_mode" — a normal
//  STA/AP mode (not promiscuous) — and the C6 acts as a
//  WiFi router for the P4's HTTP server.
//
//  Note: this is a Phase-2 stub. The C6 ↔ HTTP bridging
//  layer is itself a project; for now this app reserves the
//  app slot and prints status only.
// ============================================================

#ifndef PM_APP_FILEMGR_H
#define PM_APP_FILEMGR_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_filemgr(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_FILEMGR_H
