// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_ref_browser.h — Shared NoSQL reference browser
//
//  Three reference apps (medical, survival, trails) and the
//  baseball app share a common UX:
//    - Index file (index.json) lists entries by id+name+meta
//    - Selecting an entry loads entry_NNN.json full document
//    - Card view scrollable
//    - Optional fetch-via-Gemini for missing entries
//
//  This module provides the shared state machine. Each
//  consumer app supplies a config (category, title, keymap)
//  and the renderer.
// ============================================================

#ifndef PM_REF_BROWSER_H
#define PM_REF_BROWSER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_REF_MAX_RESULTS  128
#define PM_REF_ID_SIZE       24
#define PM_REF_NAME_SIZE     64
#define PM_REF_META_SIZE     24

typedef struct {
    char id[PM_REF_ID_SIZE];
    char name[PM_REF_NAME_SIZE];
    char meta[PM_REF_META_SIZE];
} pm_ref_entry_t;

typedef struct {
    const char* category;       // NoSQL category, e.g. "medical"
    const char* title;          // Header text
    bool        allow_fetch;    // Show "fetch via Gemini" affordance
} pm_ref_config_t;

typedef struct pm_ref_browser_s pm_ref_browser_t;

pm_ref_browser_t* pm_ref_browser_create(const pm_ref_config_t* cfg);
void              pm_ref_browser_destroy(pm_ref_browser_t* b);

// Refresh the search results list from index.json.
void              pm_ref_browser_refresh(pm_ref_browser_t* b);

// Filter results by substring (case-insensitive).
void              pm_ref_browser_set_filter(pm_ref_browser_t* b,
                                              const char* filter);

// Visible (filtered) results.
int               pm_ref_browser_visible_count(const pm_ref_browser_t* b);
const pm_ref_entry_t* pm_ref_browser_visible_at(const pm_ref_browser_t* b,
                                                  int index);

// Cursor / scroll state.
int               pm_ref_browser_cursor(const pm_ref_browser_t* b);
void              pm_ref_browser_cursor_move(pm_ref_browser_t* b, int delta);

// Open the entry at the cursor. Loads the full document into a
// PSRAM buffer; the caller can read it via pm_ref_browser_card().
bool              pm_ref_browser_open(pm_ref_browser_t* b);
const char*       pm_ref_browser_card(const pm_ref_browser_t* b);
void              pm_ref_browser_close_card(pm_ref_browser_t* b);
bool              pm_ref_browser_card_visible(const pm_ref_browser_t* b);

// Card scroll
int               pm_ref_browser_card_scroll(const pm_ref_browser_t* b);
void              pm_ref_browser_card_scroll_set(pm_ref_browser_t* b, int y);

#ifdef __cplusplus
}
#endif

#endif  // PM_REF_BROWSER_H
