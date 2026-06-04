// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_app_contacts.h — Address book
//
//  Two-pane address book. Reads contacts from:
//    1. NoSQL category "contacts" (preferred; portable from S3)
//    2. /sd/contacts.csv fallback if nosql is empty
//
//  Editing is deferred to a follow-up — this first cut is a
//  reliable reader that surfaces existing data.
// ============================================================

#ifndef PM_APP_CONTACTS_H
#define PM_APP_CONTACTS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_contacts(void);
#ifdef __cplusplus
}
#endif
#endif
