// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_etch.c — Drawing canvas
//
//  LVGL has lv_canvas which gives us a back-buffer to draw
//  pixels and lines into. Backing store is allocated in PSRAM
//  (canvas_buffer = pm_psram_alloc(W * H * 2)) at 16-bit RGB.
//
//  Logic mirrors S3 etch.cpp:
//    - Track lastX/lastY between touch events; draw a line
//      from last to current.
//    - Reset lastX/lastY on touch release.
//    - Header tap → clear (kept for parity with S3 SHAKE).
//
//  Save (.bmp): RGB565 → BGR888 conversion + BMP header,
//  written to /sd/etch/etch-<timestamp>.bmp under SPI Treaty.
// ============================================================

#include "pm_app_etch.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char* TAG = "PM_ETCH";

// Canvas geometry — leaves space for header + footer on a 1024×600 panel.
#define CANVAS_W  960
#define CANVAS_H  500

static uint16_t* s_canvas = NULL;     // PSRAM, RGB565
static int       s_last_x = -1;
static int       s_last_y = -1;
static uint16_t  s_color  = 0x0000;   // black

// LVGL handles
static void* s_screen     = NULL;
static void* s_canvas_obj = NULL;

// ─────────────────────────────────────────────
//  Canvas allocation
// ─────────────────────────────────────────────
static bool _ensure_canvas(void) {
    if (s_canvas) return true;
    size_t bytes = (size_t)CANVAS_W * CANVAS_H * 2;
    s_canvas = (uint16_t*)pm_psram_alloc(bytes);
    if (!s_canvas) {
        pm_log_e(TAG, "PSRAM alloc %u bytes failed", (unsigned)bytes);
        return false;
    }
    // Fill grey 0x7BEF
    uint16_t grey = 0x7BEF;
    for (size_t i = 0; i < (size_t)CANVAS_W * CANVAS_H; i++) s_canvas[i] = grey;
    return true;
}

// ─────────────────────────────────────────────
//  Drawing primitives — direct on the buffer
// ─────────────────────────────────────────────
static inline void _put_px(int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    s_canvas[y * CANVAS_W + x] = c;
}

static void _draw_line(int x0, int y0, int x1, int y1, uint16_t c) {
    // Bresenham
    int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        _put_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ─────────────────────────────────────────────
//  Public input — called from LVGL touch handler
// ─────────────────────────────────────────────
void pm_app_etch_touch_move(int x, int y, bool pressed) {
    if (!s_canvas) return;
    if (!pressed) { s_last_x = -1; s_last_y = -1; return; }

    if (s_last_x >= 0) {
        _draw_line(s_last_x, s_last_y, x, y, s_color);
    } else {
        _put_px(x, y, s_color);
    }
    s_last_x = x;
    s_last_y = y;

    // TODO_LVGL: lv_obj_invalidate(s_canvas_obj) or lv_canvas_set_px*
}

void pm_app_etch_clear(void) {
    if (!s_canvas) return;
    uint16_t grey = 0x7BEF;
    for (size_t i = 0; i < (size_t)CANVAS_W * CANVAS_H; i++) s_canvas[i] = grey;
    s_last_x = -1; s_last_y = -1;
    pm_log_i(TAG, "cleared");
    // TODO_LVGL: lv_obj_invalidate(s_canvas_obj)
}

void pm_app_etch_cycle_color(void) {
    static const uint16_t palette[] = {
        0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF
    };
    static int idx = 0;
    idx = (idx + 1) % (int)(sizeof(palette) / sizeof(palette[0]));
    s_color = palette[idx];
    pm_log_i(TAG, "color: 0x%04X", s_color);
}

// ─────────────────────────────────────────────
//  BMP save (RGB565 → BMP24 BGR)
// ─────────────────────────────────────────────
static bool _save_bmp(const char* path) {
    if (!s_canvas) return false;

    bool ok = false;
    PM_SPI_TAKE("etch_save") {
        pm_file_mkdir("/sd/etch");

        pm_file_t* f = pm_file_open(path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            // BMP needs 4-byte row alignment
            int row_bytes = CANVAS_W * 3;
            int padding   = (4 - (row_bytes % 4)) % 4;
            uint32_t row_stride = row_bytes + padding;
            uint32_t img_size   = row_stride * CANVAS_H;
            uint32_t file_size  = 14 + 40 + img_size;

            // BITMAPFILEHEADER (14 bytes)
            uint8_t hdr[14] = {
                'B','M',
                (uint8_t)(file_size & 0xFF),
                (uint8_t)((file_size >> 8) & 0xFF),
                (uint8_t)((file_size >> 16) & 0xFF),
                (uint8_t)((file_size >> 24) & 0xFF),
                0,0, 0,0,
                54,0,0,0
            };
            pm_file_write(f, hdr, sizeof(hdr));

            // BITMAPINFOHEADER (40 bytes)
            uint8_t bih[40] = {0};
            bih[0]  = 40;
            bih[4]  = (uint8_t)(CANVAS_W & 0xFF);
            bih[5]  = (uint8_t)((CANVAS_W >> 8) & 0xFF);
            bih[8]  = (uint8_t)(CANVAS_H & 0xFF);
            bih[9]  = (uint8_t)((CANVAS_H >> 8) & 0xFF);
            bih[12] = 1;     // planes
            bih[14] = 24;    // bpp
            pm_file_write(f, bih, sizeof(bih));

            // Pixel data — BMP rows are bottom-up
            uint8_t* row_buf = (uint8_t*)pm_psram_alloc(row_stride);
            if (row_buf) {
                memset(row_buf, 0, row_stride);
                for (int y = CANVAS_H - 1; y >= 0; y--) {
                    uint8_t* p = row_buf;
                    uint16_t* src = &s_canvas[y * CANVAS_W];
                    for (int x = 0; x < CANVAS_W; x++) {
                        uint16_t v = src[x];
                        uint8_t r = (v >> 11) & 0x1F;
                        uint8_t g = (v >> 5)  & 0x3F;
                        uint8_t b =  v        & 0x1F;
                        // 5-bit / 6-bit → 8-bit
                        *p++ = (uint8_t)((b << 3) | (b >> 2));
                        *p++ = (uint8_t)((g << 2) | (g >> 4));
                        *p++ = (uint8_t)((r << 3) | (r >> 2));
                    }
                    pm_file_write(f, row_buf, row_stride);
                }
                pm_psram_free(row_buf);
                ok = true;
            }
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    return ok;
}

void pm_app_etch_save(void) {
    char path[80];
    snprintf(path, sizeof(path), "/sd/etch/etch-%u.bmp",
             (unsigned)pm_uptime_seconds());
    bool ok = _save_bmp(path);
    pm_log_i(TAG, "%s: %s", ok ? "saved" : "save FAILED", path);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("ETCH",
        "ETCH app — UI ready");
}

static void _init(void) {
    _ensure_canvas();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _ensure_canvas();
    s_last_x = -1; s_last_y = -1;
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_canvas) { pm_psram_free(s_canvas); s_canvas = NULL; }
}

static const pm_app_t _APP = {
    .id           = "etch",
    .display_name = "ETCH",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_etch(void) { return &_APP; }
