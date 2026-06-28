// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_FUEL_GAUGE_BQ27220_H
#define PM_FUEL_GAUGE_BQ27220_H

// ============================================================
//  pm_fuel_gauge_bq27220.h — TI BQ27220 single-cell fuel gauge
//
//  The BQ27220 sits at I2C address 0x55 on the LilyGO T-Display-P4
//  and tracks the battery's state-of-charge, voltage, current,
//  remaining capacity, and reported health. It speaks a standard
//  TI gas-gauge register protocol: a 1-byte register address
//  followed by 2 little-endian data bytes.
//
//  We expose the small set of fields apps actually need (the
//  status bar, low-battery warnings, the "battery" app):
//    - state of charge (0..100 %)
//    - cell voltage (mV)
//    - instantaneous current (mA, signed; + = charge)
//    - reported temperature (0.1 K)
//    - flags (charging / discharging / fully charged)
//
//  Datasheet: TI sluuad0c BQ27220 Technical Reference Manual.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_BQ27220_ADDR  0x55

typedef struct {
    uint8_t  soc_pct;       // 0..100 — state of charge
    uint16_t voltage_mv;    // cell voltage
    int16_t  current_ma;    // signed: positive = charging
    int16_t  temperature_c10;  // tenths of a degree C
    uint16_t remaining_mah;
    uint16_t full_capacity_mah;
    bool     charging;
    bool     discharging;
    bool     fully_charged;
    bool     present;       // false if battery is missing / not detected
} pm_battery_t;

// Initialise / probe the gauge. Returns ESP_OK if the chip
// responded; ESP_ERR_NOT_FOUND if no battery + gauge subsystem
// is present (acceptable on boards without one).
esp_err_t pm_fuel_gauge_init(void);

// Sample the current battery state. Cheap to call once per second
// from the UI; safe to call from any task.
esp_err_t pm_fuel_gauge_read(pm_battery_t* out);

// Quick "is it there" check for the boot diagnostic.
bool pm_fuel_gauge_present(void);

#ifdef __cplusplus
}
#endif

#endif
