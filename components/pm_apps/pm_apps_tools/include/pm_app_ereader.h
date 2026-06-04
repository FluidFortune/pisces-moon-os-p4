// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_ereader.h — SD card text/markdown reader
//
//  Two-pane reader: book list on the left, paginated content
//  on the right. Bookmarks (scroll position per file) are
//  stored in nosql so re-opening a book lands you where you
//  left off.
// ============================================================

#ifndef PM_APP_EREADER_H
#define PM_APP_EREADER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_ereader(void);
#ifdef __cplusplus
}
#endif
#endif
