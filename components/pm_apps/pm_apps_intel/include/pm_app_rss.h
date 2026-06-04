// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_rss.h — RSS news reader
//
//  Fetches RSS 2.0 feeds over HTTP/HTTPS and surfaces items
//  in a two-pane list+detail view.
//
//  Feed list: hardcoded defaults (see pm_app_rss.c). Future
//  iteration: read /sd/rss_feeds.txt for user-supplied URLs.
// ============================================================

#ifndef PM_APP_RSS_H
#define PM_APP_RSS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_rss(void);
#ifdef __cplusplus
}
#endif
#endif
