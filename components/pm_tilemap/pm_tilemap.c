// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_tilemap.c — Slippy-map tile renderer
//
//  Implementation overview:
//
//  The tilemap is a single LVGL container at a fixed size. As
//  the centre changes (set_center, pan), we compute which tile
//  indices intersect the viewport. For each visible slot we
//  either re-use an existing cell that already holds the right
//  tile (cache hit) or re-target a free cell and load it.
//
//  ── Why raw RGB565 tiles, not PNG ──
//
//  Two device-correctness reasons drove this:
//
//    1. The SD card on the Pisces Moon P4 sits behind the
//       app-level SPI Bus Treaty mutex (pm_spi_treaty). LVGL's
//       built-in image loader would read the card OUTSIDE that
//       mutex, racing the wardrive CSV writer on the same bus.
//       Loading tiles ourselves through pm_file_* lets us hold
//       PM_SPI_TAKE for the read, so SD access stays serialized.
//
//    2. It removes any dependency on LVGL's "S:" filesystem
//       driver and a PNG decoder being enabled in the LVGL
//       Kconfig — neither is guaranteed on this build.
//
//  A tile is 256×256 pixels of little-endian RGB565 = 131072
//  bytes, stored at /sd/tiles/{z}/{x}/{y}.bin. Generate them
//  from standard OSM PNG tiles with the converter recipe in
//  the header. (CONFIG_LV_COLOR_16_SWAP=n on this board, so no
//  byte swap is needed.)
//
//  Each cell owns a PSRAM pixel buffer and an lv_image_dsc_t
//  that points at it. LVGL references the buffer directly at
//  render time (no copy), so the buffer must outlive the
//  on-screen image — we keep it until the cell is recycled to
//  a different tile or the map is destroyed.
//
//  Pan repositions every cell; a cell that scrolls off-screen
//  is re-targeted to the newly exposed edge tile and its buffer
//  reloaded. We never create/destroy LVGL objects during pan —
//  only swap pixel buffers — so the frame rate stays steady.
// ============================================================

#include "pm_tilemap.h"
#include "pm_hal.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "PM_TILEMAP";

#define TILE_PX         256
#define TILE_BYTES      (TILE_PX * TILE_PX * 2)   // RGB565
#define MAX_ZOOM        18
#define MIN_ZOOM        2
#define DEFAULT_ZOOM    14
#define MAX_TILES       40        // 5×6 plus pan margin
#define MAX_MARKERS     64
#define MAX_TRACK_PTS   256

// ── Internal tile cell ──────────────────────────────────────
typedef struct {
    lv_obj_t*       img;        // lv_image child
    lv_image_dsc_t  dsc;        // descriptor pointing at `pixels`
    uint8_t*        pixels;     // PSRAM RGB565 buffer (TILE_BYTES) or NULL
    int             tx, ty;     // tile coords this cell currently holds
    bool            loaded;     // pixels valid + dsc set as src
    bool            in_use;     // claimed this relayout pass
} tile_cell_t;

struct pm_tilemap_t {
    lv_obj_t*  root;          // container
    lv_obj_t*  tile_layer;    // child container for tile images
    lv_obj_t*  overlay_layer; // child container for markers + track

    lv_obj_t*  track_line;    // lv_line widget for the polyline

    int        w, h;
    int        zoom;
    double     lat, lon;
    char       cache_dir[48];

    tile_cell_t cells[MAX_TILES];

    pm_tilemap_marker_t markers[MAX_MARKERS];
    int                 marker_count;
    lv_obj_t*           marker_objs[MAX_MARKERS];

    pm_tilemap_marker_t track[MAX_TRACK_PTS];
    int                 track_count;
    lv_point_precise_t  track_pts[MAX_TRACK_PTS];

    bool       drag_active;
    lv_coord_t drag_last_x, drag_last_y;
};

// ── Web Mercator math ───────────────────────────────────────
static double _deg2rad(double d) { return d * (M_PI / 180.0); }

void pm_tilemap_lonlat_to_px(double lat, double lon, int z,
                                long* px, long* py) {
    if (z < 0) z = 0; if (z > MAX_ZOOM) z = MAX_ZOOM;
    double world = (double)TILE_PX * (double)(1L << z);
    double x = (lon + 180.0) / 360.0 * world;
    double sinlat = sin(_deg2rad(lat));
    if (sinlat > 0.9999) sinlat = 0.9999;
    if (sinlat < -0.9999) sinlat = -0.9999;
    double y = (0.5 - log((1.0 + sinlat) / (1.0 - sinlat)) /
                (4.0 * M_PI)) * world;
    if (px) *px = (long)x;
    if (py) *py = (long)y;
}

// ── Tile pixel loading ──────────────────────────────────────
//
// Reads /sd/tiles/{z}/{x}/{y}.bin into the cell's PSRAM buffer
// under the SPI Bus Treaty. Returns true if a full tile was
// read. A short read or a missing file leaves the cell unloaded
// (rendered blank), which is the graceful no-tiles state.
static bool _tile_read(pm_tilemap_t* m, tile_cell_t* c, int tx, int ty, int z) {
    if (!c->pixels) {
        c->pixels = (uint8_t*)heap_caps_malloc(TILE_BYTES,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!c->pixels) {
            ESP_LOGW(TAG, "tile buffer alloc failed");
            return false;
        }
    }
    char path[80];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.bin", m->cache_dir, z, tx, ty);

    size_t got = 0;
    PM_SPI_TAKE("tilemap_read") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            got = pm_file_read(f, c->pixels, TILE_BYTES);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();

    return got == TILE_BYTES;
}

// ── Cell helpers ────────────────────────────────────────────
static tile_cell_t* _cell_find(pm_tilemap_t* m, int tx, int ty) {
    for (int i = 0; i < MAX_TILES; i++) {
        if (m->cells[i].loaded &&
            m->cells[i].tx == tx && m->cells[i].ty == ty) {
            return &m->cells[i];
        }
    }
    return NULL;
}

static tile_cell_t* _cell_claim(pm_tilemap_t* m) {
    // Prefer a never-used cell; otherwise any cell not claimed
    // in this relayout pass.
    for (int i = 0; i < MAX_TILES; i++) {
        if (!m->cells[i].img) return &m->cells[i];
    }
    for (int i = 0; i < MAX_TILES; i++) {
        if (!m->cells[i].in_use) return &m->cells[i];
    }
    return &m->cells[0];
}

static void _cell_apply(pm_tilemap_t* m, tile_cell_t* c,
                         int tx, int ty, int z) {
    if (!c->img) {
        c->img = lv_image_create(m->tile_layer);
        lv_obj_clear_flag(c->img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(c->img, LV_OBJ_FLAG_SCROLLABLE);
    }
    bool ok = _tile_read(m, c, tx, ty, z);
    if (ok) {
        c->dsc.header.magic   = LV_IMAGE_HEADER_MAGIC;
        c->dsc.header.cf      = LV_COLOR_FORMAT_RGB565;
        c->dsc.header.w       = TILE_PX;
        c->dsc.header.h       = TILE_PX;
        c->dsc.header.stride  = TILE_PX * 2;
        c->dsc.data_size      = TILE_BYTES;
        c->dsc.data           = c->pixels;
        lv_image_set_src(c->img, &c->dsc);
        lv_obj_clear_flag(c->img, LV_OBJ_FLAG_HIDDEN);
        c->loaded = true;
    } else {
        // No tile on disk — show nothing for this cell.
        lv_image_set_src(c->img, NULL);
        lv_obj_add_flag(c->img, LV_OBJ_FLAG_HIDDEN);
        c->loaded = false;
    }
    c->tx = tx;
    c->ty = ty;
    c->in_use = true;
}

static void _cell_position(tile_cell_t* c, int screen_x, int screen_y) {
    if (c->img) lv_obj_set_pos(c->img, screen_x, screen_y);
}

// ── Layout & redraw ─────────────────────────────────────────
static void _relayout(pm_tilemap_t* m) {
    long center_px, center_py;
    pm_tilemap_lonlat_to_px(m->lat, m->lon, m->zoom,
                              &center_px, &center_py);

    long ox = center_px - m->w / 2;   // viewport top-left in world px
    long oy = center_py - m->h / 2;

    int t0x = (int)floor((double)ox / TILE_PX) - 1;
    int t0y = (int)floor((double)oy / TILE_PX) - 1;
    int cols = (m->w / TILE_PX) + 3;
    int rows = (m->h / TILE_PX) + 3;
    if (cols * rows > MAX_TILES) {
        cols = (m->w / TILE_PX) + 2;
        rows = (m->h / TILE_PX) + 2;
        t0x += 1; t0y += 1;
    }

    for (int i = 0; i < MAX_TILES; i++) m->cells[i].in_use = false;

    int world_tiles = 1 << m->zoom;
    int placed = 0;
    for (int r = 0; r < rows && placed < MAX_TILES; r++) {
        for (int c = 0; c < cols && placed < MAX_TILES; c++) {
            int tx = t0x + c;
            int ty = t0y + r;
            int wx = ((tx % world_tiles) + world_tiles) % world_tiles;
            int wy = ty;
            if (wy < 0 || wy >= world_tiles) continue;

            tile_cell_t* cell = _cell_find(m, wx, wy);
            if (!cell) {
                cell = _cell_claim(m);
                _cell_apply(m, cell, wx, wy, m->zoom);
            } else {
                cell->in_use = true;
            }
            int screen_x = tx * TILE_PX - (int)ox;
            int screen_y = ty * TILE_PX - (int)oy;
            _cell_position(cell, screen_x, screen_y);
            placed++;
        }
    }

    // Hide cells not claimed this pass.
    for (int i = 0; i < MAX_TILES; i++) {
        if (m->cells[i].img && !m->cells[i].in_use) {
            lv_obj_add_flag(m->cells[i].img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Markers
    for (int i = 0; i < MAX_MARKERS; i++) {
        if (i >= m->marker_count) {
            if (m->marker_objs[i]) {
                lv_obj_add_flag(m->marker_objs[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        pm_tilemap_marker_t* mk = &m->markers[i];
        long mx, my;
        pm_tilemap_lonlat_to_px(mk->lat, mk->lon, m->zoom, &mx, &my);
        int sx = (int)(mx - ox);
        int sy = (int)(my - oy);
        if (!m->marker_objs[i]) {
            lv_obj_t* dot = lv_obj_create(m->overlay_layer);
            lv_obj_remove_style_all(dot);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, lv_color_white(), 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_80, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            m->marker_objs[i] = dot;
        }
        int rr = mk->radius_px > 0 ? mk->radius_px : 6;
        lv_obj_set_size(m->marker_objs[i], rr * 2, rr * 2);
        lv_obj_set_pos(m->marker_objs[i], sx - rr, sy - rr);
        lv_obj_set_style_bg_color(m->marker_objs[i], mk->color, 0);
        lv_obj_clear_flag(m->marker_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Track polyline
    if (m->track_count >= 2) {
        for (int i = 0; i < m->track_count; i++) {
            long tx, ty;
            pm_tilemap_lonlat_to_px(m->track[i].lat, m->track[i].lon,
                                      m->zoom, &tx, &ty);
            m->track_pts[i].x = (lv_value_precise_t)(tx - ox);
            m->track_pts[i].y = (lv_value_precise_t)(ty - oy);
        }
        if (!m->track_line) {
            m->track_line = lv_line_create(m->overlay_layer);
            lv_obj_set_style_line_width(m->track_line, 3, 0);
            lv_obj_set_style_line_rounded(m->track_line, true, 0);
            lv_obj_set_style_line_color(m->track_line,
                                          lv_color_hex(0x4dd9ff), 0);
            lv_obj_clear_flag(m->track_line, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_line_set_points(m->track_line, m->track_pts, m->track_count);
        lv_obj_clear_flag(m->track_line, LV_OBJ_FLAG_HIDDEN);
    } else if (m->track_line) {
        lv_obj_add_flag(m->track_line, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Touch drag (pan) ────────────────────────────────────────
static void _root_event(lv_event_t* e) {
    pm_tilemap_t* m = (pm_tilemap_t*)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_active();
    if (!m || !indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    switch (code) {
        case LV_EVENT_PRESSED:
            m->drag_active = true;
            m->drag_last_x = p.x;
            m->drag_last_y = p.y;
            break;
        case LV_EVENT_PRESSING:
            if (m->drag_active) {
                int dx = p.x - m->drag_last_x;
                int dy = p.y - m->drag_last_y;
                if (dx || dy) {
                    pm_tilemap_pan(m, -dx, -dy);
                    m->drag_last_x = p.x;
                    m->drag_last_y = p.y;
                }
            }
            break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            m->drag_active = false;
            break;
        default: break;
    }
}

// ── Public API ──────────────────────────────────────────────
pm_tilemap_t* pm_tilemap_create(lv_obj_t* parent, int w, int h) {
    pm_tilemap_t* m = (pm_tilemap_t*)heap_caps_calloc(
        1, sizeof(*m), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m) {
        ESP_LOGE(TAG, "alloc failed");
        return NULL;
    }
    m->w = w; m->h = h;
    m->zoom = DEFAULT_ZOOM;
    m->lat = 0.0; m->lon = 0.0;
    snprintf(m->cache_dir, sizeof(m->cache_dir), "%s", "/sd/tiles");

    m->root = lv_obj_create(parent);
    lv_obj_remove_style_all(m->root);
    lv_obj_set_size(m->root, w, h);
    lv_obj_set_style_bg_color(m->root, lv_color_hex(0x060d14), 0);
    lv_obj_set_style_bg_opa(m->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m->root, lv_color_hex(0x1f4060), 0);
    lv_obj_set_style_border_width(m->root, 1, 0);
    lv_obj_clear_flag(m->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m->root, _root_event, LV_EVENT_ALL, m);

    m->tile_layer = lv_obj_create(m->root);
    lv_obj_remove_style_all(m->tile_layer);
    lv_obj_set_size(m->tile_layer, w, h);
    lv_obj_set_pos(m->tile_layer, 0, 0);
    lv_obj_set_style_bg_opa(m->tile_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(m->tile_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m->tile_layer, LV_OBJ_FLAG_SCROLLABLE);

    m->overlay_layer = lv_obj_create(m->root);
    lv_obj_remove_style_all(m->overlay_layer);
    lv_obj_set_size(m->overlay_layer, w, h);
    lv_obj_set_pos(m->overlay_layer, 0, 0);
    lv_obj_set_style_bg_opa(m->overlay_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(m->overlay_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m->overlay_layer, LV_OBJ_FLAG_SCROLLABLE);

    return m;
}

void pm_tilemap_destroy(pm_tilemap_t* m) {
    if (!m) return;
    for (int i = 0; i < MAX_TILES; i++) {
        if (m->cells[i].pixels) heap_caps_free(m->cells[i].pixels);
    }
    if (m->root) lv_obj_del(m->root);
    heap_caps_free(m);
}

lv_obj_t* pm_tilemap_obj(pm_tilemap_t* m) {
    return m ? m->root : NULL;
}

void pm_tilemap_set_cache_dir(pm_tilemap_t* m, const char* dir) {
    if (m && dir) snprintf(m->cache_dir, sizeof(m->cache_dir), "%s", dir);
}

void pm_tilemap_set_center(pm_tilemap_t* m, double lat, double lon) {
    if (!m) return;
    if (lat > 85.0)  lat = 85.0;
    if (lat < -85.0) lat = -85.0;
    while (lon > 180.0)  lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    m->lat = lat;
    m->lon = lon;
    _relayout(m);
}

void pm_tilemap_get_center(pm_tilemap_t* m, double* lat, double* lon) {
    if (!m) return;
    if (lat) *lat = m->lat;
    if (lon) *lon = m->lon;
}

void pm_tilemap_set_zoom(pm_tilemap_t* m, int z) {
    if (!m) return;
    if (z < MIN_ZOOM) z = MIN_ZOOM;
    if (z > MAX_ZOOM) z = MAX_ZOOM;
    if (z == m->zoom) return;
    // Invalidate every cell — their tile coords are for the old zoom.
    for (int i = 0; i < MAX_TILES; i++) {
        m->cells[i].loaded = false;
        m->cells[i].in_use = false;
        if (m->cells[i].img) lv_image_set_src(m->cells[i].img, NULL);
    }
    m->zoom = z;
    _relayout(m);
}

int pm_tilemap_get_zoom(pm_tilemap_t* m) {
    return m ? m->zoom : 0;
}

void pm_tilemap_resize(pm_tilemap_t* m, int w, int h) {
    if (!m || w <= 0 || h <= 0) return;
    if (w == m->w && h == m->h) return;
    m->w = w;
    m->h = h;
    lv_obj_set_size(m->root, w, h);
    lv_obj_set_size(m->tile_layer, w, h);
    lv_obj_set_size(m->overlay_layer, w, h);
    _relayout(m);
}

void pm_tilemap_pan(pm_tilemap_t* m, int dx_px, int dy_px) {
    if (!m) return;
    double world = (double)TILE_PX * (double)(1L << m->zoom);
    double dlon = (double)dx_px / world * 360.0;
    long cx, cy;
    pm_tilemap_lonlat_to_px(m->lat, m->lon, m->zoom, &cx, &cy);
    long ny = cy + dy_px;
    double n = M_PI * (1.0 - 2.0 * (double)ny / world);
    double new_lat = atan(sinh(n)) * (180.0 / M_PI);
    pm_tilemap_set_center(m, new_lat, m->lon + dlon);
}

void pm_tilemap_set_markers(pm_tilemap_t* m,
                              const pm_tilemap_marker_t* markers,
                              int count) {
    if (!m) return;
    if (count < 0) count = 0;
    if (count > MAX_MARKERS) count = MAX_MARKERS;
    if (markers && count > 0) {
        memcpy(m->markers, markers, sizeof(*markers) * count);
    }
    m->marker_count = count;
    _relayout(m);
}

void pm_tilemap_set_track(pm_tilemap_t* m,
                            const pm_tilemap_marker_t* points,
                            int count) {
    if (!m) return;
    if (count < 0) count = 0;
    if (count > MAX_TRACK_PTS) count = MAX_TRACK_PTS;
    if (points && count > 0) {
        memcpy(m->track, points, sizeof(*points) * count);
    }
    m->track_count = count;
    _relayout(m);
}

void pm_tilemap_invalidate(pm_tilemap_t* m) {
    if (m) _relayout(m);
}
