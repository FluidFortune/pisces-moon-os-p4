// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



#include "pm_app_wpa_hs.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_WPA_HS";

#define HS_DIR     "/sd/handshakes"
#define MAX_RECENT 16

typedef struct {
    char ssid[33];
    char bssid[18];
    char path[64];
    uint32_t saved_ms;
} hs_t;

static hs_t s_recent[MAX_RECENT];
static int  s_count = 0;
static int  s_total_session = 0;
static bool s_active = false;

static int _next_seq(void) {
    int max_n = 0;
    PM_SPI_TAKE("hs_seq") {
        pm_file_mkdir(HS_DIR);
        pm_dir_t* d = pm_dir_open(HS_DIR);
        if (d) {
            const char* name; bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL) {
                if (is_dir) continue;
                if (strncmp(name, "hs_", 3) != 0) continue;
                int n = atoi(name + 3);
                if (n > max_n) max_n = n;
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();
    return max_n + 1;
}

void pm_app_wpa_hs_on_capture(const char* ssid, const char* bssid,
                                const char* hccapx_b64) {
    if (!s_active || !ssid || !bssid || !hccapx_b64) return;
    int seq = _next_seq();
    char path[64];
    snprintf(path, sizeof(path), "%s/hs_%04d.hccapx", HS_DIR, seq);

    // Decode base64 into PSRAM, write binary
    size_t in_len  = strlen(hccapx_b64);
    size_t out_max = (in_len * 3) / 4 + 4;
    uint8_t* buf = (uint8_t*)pm_psram_alloc(out_max);
    if (!buf) return;
    size_t out_len = 0;
    int rc = mbedtls_base64_decode(buf, out_max, &out_len,
                                     (const unsigned char*)hccapx_b64, in_len);
    if (rc == 0) {
        PM_SPI_TAKE("hs_write") {
            pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
            if (f) {
                pm_file_write(f, buf, out_len);
                pm_file_close(f);
            }
        } PM_SPI_GIVE();
        s_total_session++;
        // Push to recent ring
        for (int i = MAX_RECENT - 1; i > 0; i--) s_recent[i] = s_recent[i - 1];
        memset(&s_recent[0], 0, sizeof(hs_t));
        strncpy(s_recent[0].ssid,  ssid,  sizeof(s_recent[0].ssid)  - 1);
        strncpy(s_recent[0].bssid, bssid, sizeof(s_recent[0].bssid) - 1);
        strncpy(s_recent[0].path,  path,  sizeof(s_recent[0].path)  - 1);
        s_recent[0].saved_ms = pm_millis();
        if (s_count < MAX_RECENT) s_count++;
        pm_log_i(TAG, "saved: %s (%s)", path, ssid);
    } else {
        pm_log_w(TAG, "base64 decode failed: %d", rc);
    }
    pm_psram_free(buf);
}

static void _start(void) {
    s_active = true;
    pm_c6_cmd_send_raw("{\"cmd\":\"wpa_hs_start\"}");
}
static void _stop(void) {
    s_active = false;
    pm_c6_cmd_send_raw("{\"cmd\":\"wpa_hs_stop\"}");
}

static void _render(void) {
    pm_log_d(TAG, "session=%d active=%d", s_total_session, s_active);
    // TODO_LVGL: status banner, [START][STOP], session count,
    //            recent captures table (ssid / bssid / file).
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("WPA HS",
        "WPA HS app — UI ready");
}
static void _init(void) { _build_screen(); }
static void _enter(void) {
    if (!s_default_screen) { _build_screen(); }
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter"); _start();
}
static void _exit_(void) { pm_log_i(TAG, "exit"); _stop(); }

static uint32_t s_last_render = 0;
static void _tick(uint32_t e) { (void)e;
    uint32_t now = pm_millis();
    if (now - s_last_render < 500) return;
    s_last_render = now; _render();
}

static const pm_app_t _APP = {
    .id           = "wpa_hs",
    .display_name = "WPA HS",
    .category     = PM_CAT_CYBER,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_wpa_hs(void) { return &_APP; }
