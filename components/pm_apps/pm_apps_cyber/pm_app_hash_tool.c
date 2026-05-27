// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_hash_tool.c — SHA256/MD5/CRC32 over text or file
//
//  Used for chain-of-custody. The PiscesMoon vision doc
//  describes session CSVs being audit-deliverable with
//  documented SHA256 hashes — this app produces those hashes.
//
//  ESP-IDF's mbedTLS provides SHA256/MD5; CRC32 is built-in.
// ============================================================

#include "pm_app_hash_tool.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md5.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_HASH";

typedef enum { ALG_SHA256, ALG_MD5, ALG_CRC32 } alg_t;

static alg_t s_alg = ALG_SHA256;
static char  s_input[1024] = "";
static int   s_input_len = 0;
static char  s_result[80] = "";
static char  s_file_path[160] = "";

static void _hex(const uint8_t* b, int n, char* out) {
    for (int i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

static void _hash_text(void) {
    if (s_alg == ALG_SHA256) {
        uint8_t out[32];
        mbedtls_sha256((const unsigned char*)s_input, s_input_len, out, 0);
        _hex(out, 32, s_result);
    } else if (s_alg == ALG_MD5) {
        uint8_t out[16];
        mbedtls_md5((const unsigned char*)s_input, s_input_len, out);
        _hex(out, 16, s_result);
    } else {
        uint32_t c = pm_crc32((const uint8_t*)s_input, s_input_len);
        snprintf(s_result, sizeof(s_result), "%08x", (unsigned)c);
    }
    pm_log_i(TAG, "hash(%d) = %s", (int)s_alg, s_result);
}

static void _hash_file(const char* path) {
    if (!path || !path[0]) return;
    PM_SPI_TAKE("hash_file") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (!f) {
            strcpy(s_result, "(file not found)");
        } else {
            uint8_t buf[1024];
            if (s_alg == ALG_SHA256) {
                mbedtls_sha256_context ctx;
                mbedtls_sha256_init(&ctx);
                mbedtls_sha256_starts(&ctx, 0);
                size_t n;
                while ((n = pm_file_read(f, buf, sizeof(buf))) > 0)
                    mbedtls_sha256_update(&ctx, buf, n);
                uint8_t out[32];
                mbedtls_sha256_finish(&ctx, out);
                mbedtls_sha256_free(&ctx);
                _hex(out, 32, s_result);
            } else if (s_alg == ALG_MD5) {
                mbedtls_md5_context ctx;
                mbedtls_md5_init(&ctx);
                mbedtls_md5_starts(&ctx);
                size_t n;
                while ((n = pm_file_read(f, buf, sizeof(buf))) > 0)
                    mbedtls_md5_update(&ctx, buf, n);
                uint8_t out[16];
                mbedtls_md5_finish(&ctx, out);
                mbedtls_md5_free(&ctx);
                _hex(out, 16, s_result);
            } else {
                uint32_t c = 0;
                size_t n;
                while ((n = pm_file_read(f, buf, sizeof(buf))) > 0)
                    c = pm_crc32_update(c, buf, n);
                snprintf(s_result, sizeof(s_result), "%08x", (unsigned)c);
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    pm_log_i(TAG, "file hash = %s", s_result);
}

void pm_app_hash_tool_set_alg(int alg) { s_alg = (alg_t)alg; }
void pm_app_hash_tool_run_text(void)   { _hash_text(); }
void pm_app_hash_tool_run_file(void)   { _hash_file(s_file_path); }

void pm_app_hash_tool_input_char(char c) {
    if (c == 8 || c == 127) { if (s_input_len > 0) s_input_len--; }
    else if (s_input_len < (int)sizeof(s_input) - 1) s_input[s_input_len++] = c;
    s_input[s_input_len] = 0;
}

static void _render(void) {
    pm_log_d(TAG, "alg=%d in=%d res=%s", (int)s_alg, s_input_len, s_result);
    // TODO_LVGL: alg picker (SHA256/MD5/CRC32 segments),
    //            text input area, file path field with [BROWSE],
    //            [HASH TEXT] / [HASH FILE] buttons, big result row.
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("HASH TOOL",
        "HASH TOOL app — UI ready");
}
static void _init(void)  { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
}
static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "hash_tool",
    .display_name = "HASH TOOL",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_hash_tool(void) { return &_APP; }
