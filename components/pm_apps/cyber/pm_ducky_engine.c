// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_ducky_engine.c
//
//  Line-by-line interpreter. Tokens parsed in-place over a
//  mutable copy held in PSRAM. Combination keys (e.g.
//  "GUI r") are sent as a single composed key, where the
//  send_key callback can decide whether that means a
//  modifier+key chord or sequential keys.
// ============================================================

#include "pm_ducky_engine.h"
#include "pm_hal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

static const char* TAG = "PM_DUCKY";

#define LINE_BUF_MAX 1024

static uint32_t s_default_delay_ms = 0;

static void _no_send_string(const char* s) { (void)s; }
static void _no_send_key   (const char* k) { (void)k; }
static void _no_delay      (uint32_t ms)   { pm_delay_ms(ms); }
static void _no_log        (const char* l) { (void)l; }

static const pm_ducky_iface_t _DEFAULT = {
    .send_string = _no_send_string,
    .send_key    = _no_send_key,
    .delay_ms    = _no_delay,
    .log_line    = _no_log,
};

static const pm_ducky_iface_t* _resolve(const pm_ducky_iface_t* in) {
    static pm_ducky_iface_t resolved;
    resolved = _DEFAULT;
    if (in) {
        if (in->send_string) resolved.send_string = in->send_string;
        if (in->send_key)    resolved.send_key    = in->send_key;
        if (in->delay_ms)    resolved.delay_ms    = in->delay_ms;
        if (in->log_line)    resolved.log_line    = in->log_line;
    }
    return &resolved;
}

static bool _is_modifier(const char* tok) {
    return strcasecmp(tok, "GUI")    == 0 ||
           strcasecmp(tok, "WINDOWS") == 0 ||
           strcasecmp(tok, "CTRL")    == 0 ||
           strcasecmp(tok, "CONTROL") == 0 ||
           strcasecmp(tok, "ALT")     == 0 ||
           strcasecmp(tok, "SHIFT")   == 0;
}

static bool _is_named_key(const char* tok) {
    static const char* keys[] = {
        "ENTER", "TAB", "ESC", "ESCAPE", "SPACE", "DELETE", "BACKSPACE",
        "UP", "DOWN", "LEFT", "RIGHT", "HOME", "END", "PAGEUP", "PAGEDOWN",
        "INSERT", "PRINTSCREEN", "PAUSE", "CAPSLOCK",
        "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12", NULL
    };
    for (int i = 0; keys[i]; i++)
        if (strcasecmp(tok, keys[i]) == 0) return true;
    return false;
}

static void _process_line(char* line, const pm_ducky_iface_t* iface,
                            char* last_line, size_t last_cap) {
    if (!line) return;

    // Trim leading WS
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == 0) return;

    // Comments
    if (strncasecmp(line, "REM", 3) == 0 && (line[3] == ' ' || line[3] == '\t' || line[3] == 0))
        return;

    iface->log_line(line);

    // REPEAT n  — re-run last_line n times
    if (strncasecmp(line, "REPEAT", 6) == 0) {
        int n = atoi(line + 7);
        for (int i = 0; i < n; i++) {
            char tmp[LINE_BUF_MAX];
            strncpy(tmp, last_line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            _process_line(tmp, iface, last_line, last_cap);
        }
        return;
    }

    // Save line for REPEAT
    strncpy(last_line, line, last_cap - 1);
    last_line[last_cap - 1] = 0;

    // STRING ...
    if (strncasecmp(line, "STRING", 6) == 0 && (line[6] == ' ' || line[6] == '\t')) {
        const char* s = line + 7;
        while (*s == ' ' || *s == '\t') s++;
        iface->send_string(s);
        if (s_default_delay_ms) iface->delay_ms(s_default_delay_ms);
        return;
    }

    // DELAY n
    if (strncasecmp(line, "DELAY", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
        uint32_t ms = (uint32_t)atoi(line + 6);
        iface->delay_ms(ms);
        return;
    }

    // DEFAULTDELAY / DEFAULT_DELAY n
    if (strncasecmp(line, "DEFAULTDELAY",  12) == 0 ||
        strncasecmp(line, "DEFAULT_DELAY", 13) == 0) {
        const char* p = strchr(line, ' ');
        if (p) s_default_delay_ms = (uint32_t)atoi(p + 1);
        return;
    }

    // Otherwise: token sequence — modifiers + key. Pass joined.
    iface->send_key(line);
    if (s_default_delay_ms) iface->delay_ms(s_default_delay_ms);
}

bool pm_ducky_run(const char* script, const pm_ducky_iface_t* iface) {
    if (!script) return false;
    iface = _resolve(iface);
    s_default_delay_ms = 0;

    size_t n = strlen(script);
    char* buf = (char*)pm_psram_alloc(n + 1);
    if (!buf) return false;
    memcpy(buf, script, n + 1);

    char last_line[LINE_BUF_MAX] = "";
    char* p = buf;
    while (*p) {
        char* eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char saved = *eol;
        *eol = 0;
        _process_line(p, iface, last_line, sizeof(last_line));
        *eol = saved;
        if (*eol == 0) break;
        p = eol + 1;
        while (*p == '\n' || *p == '\r') p++;
    }

    pm_psram_free(buf);
    pm_log_i(TAG, "script done");
    return true;
}
