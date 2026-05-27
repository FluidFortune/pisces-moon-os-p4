// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_camera.c
//
//  Scaffolded against Espressif's esp_video managed component.
//  The exact sensor identification needs hardware bring-up, so
//  this file provides the right API surface and the right
//  probe / present pattern; the actual V4L2 plumbing is filled
//  in once the board is on the bench.
//
//  Design notes:
//    - All frame buffers come from PSRAM (RGB565 viewfinder
//      alone at 800×600 is 960KB).
//    - JPEG snapshots write directly through PM_SPI_TAKE to SD.
//    - The stream task runs at LV_TASK_PRIO-1 so the UI keeps
//      priority during viewfinder updates.
// ============================================================

#include "pm_camera.h"
#include "pm_hal.h"
#include "pm_peer.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_CAMERA";

static bool s_present = false;
static char s_sensor_name[24] = "(none)";
static pm_camera_frame_cb_t s_stream_cb = NULL;
static void*               s_stream_user = NULL;
static bool                s_streaming = false;

// Capabilities advertised by camera peer
static const char* CAM_CAPS[] = {
    "camera_snapshot", "camera_stream", "camera_barcode", NULL,
};

bool pm_camera_probe(void) {
    // TODO_HW: call esp_video probe routine — needs the actual
    // sensor I2C address. Likely candidates per ELECROW's BOM
    // for this connector: SC2336 (0x36), OV2640 (0x30).
    //
    // For the alpha, mark not-present so apps degrade gracefully.
    // First-boot bring-up will fill this in.
    s_present = false;
    strncpy(s_sensor_name, "(none)", sizeof(s_sensor_name) - 1);

    if (s_present) {
        pm_peer_announce(PM_PEER_KIND_CAMERA_CSI,
                          s_sensor_name, CAM_CAPS);
        pm_log_i(TAG, "Camera detected: %s", s_sensor_name);
    } else {
        pm_log_i(TAG, "No camera detected (modular: features hidden)");
    }
    return s_present;
}

bool        pm_camera_present     (void) { return s_present; }
const char* pm_camera_sensor_name (void) { return s_sensor_name; }

int pm_camera_stream_start(int w, int h, pm_camera_pixfmt_t fmt,
                            pm_camera_frame_cb_t cb, void* user) {
    if (!s_present) {
        pm_log_w(TAG, "stream_start called but no camera present");
        return -1;
    }
    (void)w; (void)h; (void)fmt;
    s_stream_cb   = cb;
    s_stream_user = user;
    s_streaming   = true;
    // TODO_HW: configure V4L2 device for w×h fmt, spawn stream task
    pm_log_i(TAG, "stream start %dx%d fmt=%d", w, h, fmt);
    return 0;
}

void pm_camera_stream_stop(void) {
    if (!s_streaming) return;
    s_streaming   = false;
    s_stream_cb   = NULL;
    s_stream_user = NULL;
    // TODO_HW: stop V4L2 device
}

int pm_camera_snapshot_to_sd(const char* filename) {
    if (!s_present || !filename) return -1;
    pm_log_i(TAG, "snapshot → /sd/photos/%s", filename);
    // TODO_HW: capture JPEG frame, write to /sd/photos/ under
    // PM_SPI_TAKE("camera_snap") { ... } PM_SPI_GIVE();
    return 0;
}
