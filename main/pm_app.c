// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app.c — App registry
//
//  Static array of registered apps. Capacity covers all S3
//  apps (~40) with headroom. Apps register at boot via
//  pm_app_register() — typically called from main_register_apps()
//  after pm_hal_init().
// ============================================================

#include "pm_app.h"
#include "pm_hal.h"
#include <string.h>

#define PM_APP_REGISTRY_MAX  64

static const char* TAG = "PM_APP";

static const pm_app_t* s_apps[PM_APP_REGISTRY_MAX];
static int             s_app_count   = 0;
static const pm_app_t* s_current_app = NULL;

bool pm_app_register(const pm_app_t* app) {
    if (!app || !app->id) return false;
    if (s_app_count >= PM_APP_REGISTRY_MAX) {
        pm_log_e(TAG, "registry full (%d)", PM_APP_REGISTRY_MAX);
        return false;
    }
    // Reject duplicate ids
    for (int i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i]->id, app->id) == 0) {
            pm_log_w(TAG, "duplicate app id '%s'", app->id);
            return false;
        }
    }
    s_apps[s_app_count++] = app;
    pm_log_i(TAG, "registered '%s' (cat=%d)", app->id, (int)app->category);
    return true;
}

const pm_app_t* pm_app_find(const char* id) {
    if (!id) return NULL;
    for (int i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) return s_apps[i];
    }
    return NULL;
}

int pm_app_count(void) {
    return s_app_count;
}

const pm_app_t* pm_app_at(int index) {
    if (index < 0 || index >= s_app_count) return NULL;
    return s_apps[index];
}

int pm_app_count_in_category(pm_category_t cat) {
    int n = 0;
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i]->category == cat) n++;
    }
    return n;
}

const pm_app_t* pm_app_in_category(pm_category_t cat, int index) {
    int n = 0;
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i]->category == cat) {
            if (n == index) return s_apps[i];
            n++;
        }
    }
    return NULL;
}

const pm_app_t* pm_app_current(void) {
    return s_current_app;
}

void pm_app_set_current(const pm_app_t* app) {
    s_current_app = app;
}
