// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_HAPTIC_AW86224_H
#define PM_HAPTIC_AW86224_H

// ============================================================
//  pm_haptic_aw86224.h — AWINIC AW86224 LRA haptic driver
//
//  The AW86224 (I2C address 0x58 on the LilyGO T-Display-P4's
//  I2C2 bus) drives the on-board linear resonant actuator. We
//  expose a tiny API: play one of a handful of named effects.
//  The chip has an extensive on-chip waveform library (~127
//  pre-stored effects); we use the most common subset so apps
//  don't have to think in waveform indices.
//
//  Typical usage: pm_haptic_play(PM_HAPTIC_TAP) on a button
//  press, pm_haptic_play(PM_HAPTIC_ALERT) on a notification.
//
//  Datasheet: AWINIC AW86224 V1.0 (LRA haptic driver IC).
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_AW86224_ADDR  0x58

// Named effect set. Maps onto the AW86224's built-in waveform
// library — the indices come from the AWINIC ROM table. This
// list is intentionally short; apps that need richer haptic
// vocabularies can add entries and matching indices below.
typedef enum {
    PM_HAPTIC_TAP        = 1,   // crisp 0.5g tap, ~30 ms
    PM_HAPTIC_CLICK      = 7,   // softer button click, ~50 ms
    PM_HAPTIC_DOUBLE_TAP = 10,  // two quick taps
    PM_HAPTIC_ALERT      = 14,  // 100 ms alert buzz
    PM_HAPTIC_NOTIFY     = 16,  // longer notification pattern
    PM_HAPTIC_LONG_BUZZ  = 47,  // ~400 ms continuous
    PM_HAPTIC_SUCCESS    = 49,  // rising tone-like pattern
    PM_HAPTIC_ERROR      = 52,  // falling tone-like pattern
} pm_haptic_effect_t;

// Initialise the driver. Probes the chip; returns ESP_ERR_NOT_FOUND
// if absent (boards without a haptic motor — graceful no-op).
esp_err_t pm_haptic_init(void);

// True if init succeeded and the chip is reachable.
bool pm_haptic_present(void);

// Play one of the named effects. Non-blocking — returns as soon
// as the chip has accepted the command; the actual vibration
// completes on the chip's own timing.
esp_err_t pm_haptic_play(pm_haptic_effect_t effect);

// Play a raw waveform-library index (0..127). Useful for
// experimentation; the named API above is preferred for
// stable apps.
esp_err_t pm_haptic_play_raw(uint8_t waveform_index);

// Stop any currently-playing waveform immediately.
esp_err_t pm_haptic_stop(void);

#ifdef __cplusplus
}
#endif

#endif
