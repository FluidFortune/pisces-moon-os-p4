// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_camera.h — CSI camera support (modular, P4-direct)
//
//  The ELECROW CrowPanel Advanced 7" has a dedicated MIPI-CSI
//  ribbon connector (silkscreen: CIS-CAM) for a small camera
//  module. The ESP32-P4 has native MIPI-CSI silicon — no
//  external bridge needed.
//
//  This component sits on top of Espressif's esp_video managed
//  component, which provides V4L2-style streaming. Apps get a
//  RGB565 frame buffer for the viewfinder, or a JPEG-encoded
//  snapshot for SD.
//
//  Modularity:
//    pm_camera_probe() at boot tries to bring up the sensor.
//    If absent, pm_camera_present() returns false and apps that
//    depend on it (pm_app_camera, pm_app_camera_qr) show
//    "no camera detected" rather than crashing.
//
//  Typical sensors on this connector: SC2336 (2MP), OV2640.
//  esp_video discovers via I2C probe on the sensor bus.
// ============================================================

#ifndef PM_CAMERA_H
#define PM_CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_CAM_PIXFMT_RGB565 = 0,
    PM_CAM_PIXFMT_JPEG   = 1,
    PM_CAM_PIXFMT_RAW8   = 2,
} pm_camera_pixfmt_t;

typedef struct {
    int       width;
    int       height;
    pm_camera_pixfmt_t fmt;
    uint8_t*  data;
    size_t    len;
    uint32_t  timestamp_ms;
} pm_camera_frame_t;

// ── Lifecycle ────────────────────────────────────────────────
// Probe the CSI sensor over the dedicated I2C bus. Returns true
// if a sensor was identified. Safe to call multiple times.
bool pm_camera_probe(void);

// Result of last probe.
bool pm_camera_present(void);

// Sensor name (e.g. "SC2336", "OV2640", or "(none)").
const char* pm_camera_sensor_name(void);

// ── Streaming ────────────────────────────────────────────────
// Start a viewfinder stream at a given size/fmt. Frames are
// delivered to `cb` from an internal task. Returns 0 on success.
typedef void (*pm_camera_frame_cb_t)(const pm_camera_frame_t*, void*);
int  pm_camera_stream_start(int w, int h, pm_camera_pixfmt_t fmt,
                              pm_camera_frame_cb_t cb, void* user);
void pm_camera_stream_stop(void);

// ── Snapshot ─────────────────────────────────────────────────
// Capture a single JPEG frame and write to /sd/photos/<name>.jpg
// Returns 0 on success.
int  pm_camera_snapshot_to_sd(const char* filename);

#ifdef __cplusplus
}
#endif

#endif  // PM_CAMERA_H
