// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_calendar.h — Monthly calendar with date notes
//
//  - Highlights today via pm_time_now().
//  - Prev / Next month navigation.
//  - "T" jumps to today.
//  - Notes per day: stored in /sd/cal/YYYY-MM-DD.txt.
//    Saved via SPI Treaty-wrapped writes when user edits.
// ============================================================

#ifndef PM_APP_CALENDAR_H
#define PM_APP_CALENDAR_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_calendar(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_CALENDAR_H
