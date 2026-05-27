// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app.h — Common app interface
//
//  Every app on Pisces Moon P4 implements pm_app_t. The
//  launcher holds an array of registered apps and calls
//  these function pointers for lifecycle events.
//
//  Lifecycle:
//    init    — once at boot. Allocate persistent state.
//              May be NULL if app has no boot-time setup.
//    enter   — every time user opens the app. Build/show LVGL
//              screen. Subscribe to inputs.
//    tick    — periodic from main loop. Optional. ms = elapsed
//              since last tick.
//    exit    — every time user backs out. Unsubscribe inputs,
//              hide screen, free transient state.
//    deinit  — once at shutdown. Rare. May be NULL.
//
//  Apps should NOT do heavy work in init() — that runs at
//  boot and delays the launcher. Defer heavy alloc to enter().
// ============================================================

#ifndef PM_APP_H
#define PM_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── App categories — match S3 launcher layout ────────────────
typedef enum {
    PM_CAT_COMMS  = 0,
    PM_CAT_CYBER  = 1,
    PM_CAT_TOOLS  = 2,
    PM_CAT_GAMES  = 3,
    PM_CAT_INTEL  = 4,
    PM_CAT_MEDIA  = 5,
    PM_CAT_SYSTEM = 6,
    PM_CAT_COUNT
} pm_category_t;

// ── App lifecycle hooks ──────────────────────────────────────
typedef struct {
    const char*    id;          // Internal id, e.g. "wardrive"
    const char*    display_name;// Launcher label, e.g. "WARDRIVE"
    pm_category_t  category;
    uint16_t       icon_id;     // LVGL image symbol id, 0 = default

    void (*init)(void);
    void (*enter)(void);
    void (*tick)(uint32_t elapsed_ms);
    void (*exit)(void);
    void (*deinit)(void);
} pm_app_t;

// ── Registration ─────────────────────────────────────────────
// Apps call this from a constructor or from main during boot.
// Returns false if the registry is full or app id is duplicate.
bool pm_app_register(const pm_app_t* app);

// Lookup
const pm_app_t* pm_app_find(const char* id);
int             pm_app_count(void);
const pm_app_t* pm_app_at(int index);
int             pm_app_count_in_category(pm_category_t cat);
const pm_app_t* pm_app_in_category(pm_category_t cat, int index);

// ── Currently active app (set by launcher) ───────────────────
const pm_app_t* pm_app_current(void);
void            pm_app_set_current(const pm_app_t* app);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_H
