// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_ssh.c — SSH client (Phase-4 scaffold)
//
//  Profile management via NoSQL ("ssh_profiles" category).
//  Terminal scrollback (PSRAM). Send/receive runs through
//  C6 TCP transport once available.
// ============================================================

#include "pm_app_ssh.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_nosql.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_SSH";

#define MAX_PROFILES 16
#define ID_SIZE      32
#define SCROLLBACK_SZ (64 * 1024)

static char  s_profile_ids[MAX_PROFILES * ID_SIZE];
static int   s_profile_count = 0;
static int   s_profile_cursor = 0;

static char* s_scrollback = NULL;
static int   s_scrollback_len = 0;

static bool  s_connected = false;
static char  s_session_label[80] = "";

// ─────────────────────────────────────────────
//  Profile I/O (NoSQL JSON)
// ─────────────────────────────────────────────
static bool _profile_save(const pm_ssh_profile_t* p) {
    if (!p) return false;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", p->name);
    cJSON_AddStringToObject(root, "host", p->host);
    cJSON_AddNumberToObject(root, "port", p->port);
    cJSON_AddStringToObject(root, "user", p->user);
    cJSON_AddStringToObject(root, "auth", p->auth);
    char* json = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (json) {
        ok = pm_nosql_write("ssh_profiles", p->name, json, strlen(json));
        cJSON_free(json);
    }
    cJSON_Delete(root);
    return ok;
}

static bool _profile_load(const char* id, pm_ssh_profile_t* out) {
    if (!id || !out) return false;
    char buf[512];
    size_t got = pm_nosql_read("ssh_profiles", id, buf, sizeof(buf));
    if (got == 0) return false;
    cJSON* root = cJSON_Parse(buf);
    if (!root) return false;
    memset(out, 0, sizeof(*out));
    out->port = 22;
    cJSON* v;
    if ((v = cJSON_GetObjectItem(root, "name")) && cJSON_IsString(v))
        strncpy(out->name, v->valuestring, sizeof(out->name) - 1);
    if ((v = cJSON_GetObjectItem(root, "host")) && cJSON_IsString(v))
        strncpy(out->host, v->valuestring, sizeof(out->host) - 1);
    if ((v = cJSON_GetObjectItem(root, "port")) && cJSON_IsNumber(v))
        out->port = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "user")) && cJSON_IsString(v))
        strncpy(out->user, v->valuestring, sizeof(out->user) - 1);
    if ((v = cJSON_GetObjectItem(root, "auth")) && cJSON_IsString(v))
        strncpy(out->auth, v->valuestring, sizeof(out->auth) - 1);
    cJSON_Delete(root);
    return true;
}

static void _refresh_profiles(void) {
    s_profile_count = pm_nosql_list("ssh_profiles",
                                     s_profile_ids, MAX_PROFILES, ID_SIZE);
    pm_log_i(TAG, "%d saved profiles", s_profile_count);
}

// Public — UI calls these
void pm_app_ssh_profile_save(const pm_ssh_profile_t* p) {
    if (_profile_save(p)) {
        _refresh_profiles();
    }
}

void pm_app_ssh_profile_delete_at_cursor(void) {
    if (s_profile_cursor < 0 || s_profile_cursor >= s_profile_count) return;
    const char* id = &s_profile_ids[s_profile_cursor * ID_SIZE];
    pm_nosql_delete("ssh_profiles", id);
    _refresh_profiles();
}

// ─────────────────────────────────────────────
//  Connect (stub)
// ─────────────────────────────────────────────
static void _sb_print(const char* s) {
    if (!s_scrollback || !s) return;
    size_t add = strlen(s);
    if (s_scrollback_len + add + 1 >= SCROLLBACK_SZ) {
        int half = SCROLLBACK_SZ / 2;
        memmove(s_scrollback, s_scrollback + half, s_scrollback_len - half);
        s_scrollback_len -= half;
    }
    memcpy(s_scrollback + s_scrollback_len, s, add);
    s_scrollback_len += add;
    s_scrollback[s_scrollback_len] = 0;
}

void pm_app_ssh_connect_at_cursor(void) {
    if (s_profile_cursor < 0 || s_profile_cursor >= s_profile_count) return;
    const char* id = &s_profile_ids[s_profile_cursor * ID_SIZE];
    pm_ssh_profile_t p;
    if (!_profile_load(id, &p)) return;

    snprintf(s_session_label, sizeof(s_session_label),
             "%s@%s:%d", p.user, p.host, p.port);
    _sb_print("Connecting to ");
    _sb_print(s_session_label);
    _sb_print("\n[ssh transport pending — needs C6 TCP + libssh port]\n");
    s_connected = false;
    pm_log_w(TAG, "ssh connect not yet implemented");
}

void pm_app_ssh_disconnect(void) {
    if (!s_connected) return;
    s_connected = false;
    _sb_print("[disconnected]\n");
}

void pm_app_ssh_send(const char* line) {
    if (!s_connected) {
        _sb_print("[not connected]\n");
        return;
    }
    // TODO: forward through C6 TCP stream once libssh port lands.
    _sb_print(line);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("SSH",
        "SSH app — UI ready");
}

static void _init(void) {
    s_scrollback = (char*)pm_psram_alloc(SCROLLBACK_SZ);
    if (s_scrollback) { s_scrollback[0] = 0; s_scrollback_len = 0; }
    pm_nosql_init("ssh_profiles");
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _refresh_profiles();
}

static void _exit_(void)  { pm_log_i(TAG, "exit"); pm_app_ssh_disconnect(); }
static void _deinit(void) { if (s_scrollback) { pm_psram_free(s_scrollback); s_scrollback = NULL; } }

static const pm_app_t _APP = {
    .id           = "ssh",
    .display_name = "SSH",
    .category     = PM_CAT_INTEL,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_ssh(void) { return &_APP; }
