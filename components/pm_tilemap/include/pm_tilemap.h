// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_tilemap.h — Slippy-map tile renderer for LVGL
//
//  A lightweight web-mercator tile renderer that displays
//  pre-cached map tiles from the SD card. Apps use it to
//  show GPS positions, wardrive sweeps, hiking trails, etc.
//
//  Cache layout on /sd/tiles/ :
//    /sd/tiles/{z}/{x}/{y}.png         (OpenStreetMap-style)
//
//  Where {z} is zoom level (0–18), {x}/{y} are slippy tile
//  coordinates. Standard tools (mobile atlas creator,
//  tilemill, etc.) all generate this layout.
//
//  Sizing:
//    A typical 800×480 (5") or 1024×600 (7") panel hosts
//    a map widget around 600×400 px. With 256-pixel tiles
//    that's 3×2 visible plus a 1-tile pan margin = ~20 tiles
//    held in memory at once (~5 MB PSRAM at RGB565).
//
//  Interaction:
//    - Touch drag pans
//    - Apps add +/- buttons to drive set_zoom
//    - No pinch zoom (LVGL 9 gesture surface is fiddly)
// ============================================================

#ifndef PM_TILEMAP_H
#define PM_TILEMAP_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_tilemap_t pm_tilemap_t;

// Marker overlay — fixed lat/lon location, drawn on top of tiles.
typedef struct {
    double      lat;
    double      lon;
    lv_color_t  color;
    int         radius_px;   // 0 → default of 6
    const char* label;       // optional, NULL skips text
} pm_tilemap_marker_t;

// Create a tilemap as a child of `parent`, sized w×h pixels.
// Returns NULL on alloc failure. Default zoom is 14, default
// centre is the prime meridian; apps should call set_center
// before showing.
pm_tilemap_t* pm_tilemap_create(lv_obj_t* parent, int w, int h);
void          pm_tilemap_destroy(pm_tilemap_t* map);

// Pull the underlying LVGL object — for adding event handlers,
// repositioning, etc. The tilemap is a single container; tiles
// and markers are children of it.
lv_obj_t* pm_tilemap_obj(pm_tilemap_t* map);

// Tile cache directory. Default "/sd/tiles". Path must persist
// for the lifetime of the map (we store the pointer).
void pm_tilemap_set_cache_dir(pm_tilemap_t* map, const char* dir);

// Center / zoom control. Lat/lon are WGS-84 decimal degrees.
// Zoom levels 0..18; out-of-range values are clamped.
void   pm_tilemap_set_center(pm_tilemap_t* map, double lat, double lon);
void   pm_tilemap_get_center(pm_tilemap_t* map, double* lat, double* lon);
void   pm_tilemap_set_zoom(pm_tilemap_t* map, int z);
int    pm_tilemap_get_zoom(pm_tilemap_t* map);

// Resize the map to w×h pixels. Use this when the host panel's
// flex layout settles after build (the real pixel size isn't
// known at create time). Triggers a relayout.
void   pm_tilemap_resize(pm_tilemap_t* map, int w, int h);

// Pan by `dx_px,dy_px` screen pixels. Negative dx pans west.
void pm_tilemap_pan(pm_tilemap_t* map, int dx_px, int dy_px);

// Replace the marker overlay. The array is copied; safe to free
// after calling. Pass count=0 to clear.
void pm_tilemap_set_markers(pm_tilemap_t* map,
                              const pm_tilemap_marker_t* markers,
                              int count);

// Replace the polyline track overlay (your route through the world).
// Drawn as a connected sequence of segments.
void pm_tilemap_set_track(pm_tilemap_t* map,
                            const pm_tilemap_marker_t* points,
                            int count);

// Force a re-layout. The map redraws lazily after set_center,
// set_zoom, pan, set_markers, set_track — apps don't normally
// need to call this.
void pm_tilemap_invalidate(pm_tilemap_t* map);

// Helper — convert lat/lon to absolute pixel coords at zoom z,
// using the standard web-mercator projection. Exposed for apps
// that want to do their own overlay positioning.
void pm_tilemap_lonlat_to_px(double lat, double lon, int z,
                               long* px, long* py);

#ifdef __cplusplus
}
#endif

#endif // PM_TILEMAP_H
