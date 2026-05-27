// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_ducky_engine.h — Shared Ducky Script parser
//
//  The three Ducky apps (BLE / USB / WiFi) all parse the same
//  scripting language (Ducky Script v1 + a few v2-isms).
//  This module is the parser; consumers supply a "send key"
//  callback that varies per transport.
//
//  Supported:
//    STRING <text>               — type literal
//    ENTER / GUI / WINDOWS / CTRL / ALT / SHIFT / TAB / ...
//    DELAY <ms>                  — wait
//    DEFAULTDELAY / DEFAULT_DELAY <ms>
//    REM <comment>               — ignored
//    REPEAT <n>                  — repeat last line n times
// ============================================================

#ifndef PM_DUCKY_ENGINE_H
#define PM_DUCKY_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_ducky_iface_s pm_ducky_iface_t;

struct pm_ducky_iface_s {
    void  (*send_string)(const char* text);
    void  (*send_key)   (const char* key_name);     // "ENTER", "F4", "GUI", ...
    void  (*delay_ms)   (uint32_t ms);
    void  (*log_line)   (const char* line);          // optional, for UI feedback
};

// Run a Ducky Script from a NUL-terminated buffer.
// Returns true if the script ran to completion.
// Pass NULL for any field of iface to no-op it.
bool pm_ducky_run(const char* script, const pm_ducky_iface_t* iface);

#ifdef __cplusplus
}
#endif

#endif  // PM_DUCKY_ENGINE_H
