// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_RTC_PCF8563_H
#define PM_RTC_PCF8563_H

// ============================================================
//  pm_rtc_pcf8563.h — NXP PCF8563 real-time clock driver
//
//  Battery-backed I2C RTC that lives at address 0x51 on the
//  LilyGO T-Display-P4's I2C1 bus. The chip is dumb but reliable:
//  it ticks BCD-encoded calendar time while the rest of the
//  device is off, and serves as the seed for the P4's system
//  clock at boot so newly-created files have the right timestamps
//  even before NTP / GPS can lock.
//
//  Pisces Moon uses it for: persistent wall-clock time across
//  reboots and battery swaps; alarm wakeups; a low-jitter
//  reference for periodic apps (pm_clock, calendar reminders).
//
//  Hardware notes:
//    - I2C address 0x51 (fixed)
//    - Register map: §8 of NXP PCF8563 datasheet
//    - BCD-encoded fields with status flags in the high bit
//    - VL flag (Voltage Low) in the seconds register signals
//      a battery-backed clock that has lost time
//    - 24-hour mode (no AM/PM)
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_PCF8563_ADDR  0x51

typedef struct {
    uint16_t year;     // full year (e.g. 2026)
    uint8_t  month;    // 1..12
    uint8_t  day;      // 1..31
    uint8_t  weekday;  // 0=Sunday..6=Saturday
    uint8_t  hour;     // 0..23
    uint8_t  minute;   // 0..59
    uint8_t  second;   // 0..59
    bool     valid;    // false if VL flag set (clock lost time)
} pm_rtc_time_t;

// Initialise the RTC. Reads the control registers, clears any
// stale alarm flags, and confirms the clock is ticking. Returns
// the VL ("voltage lost") flag in *clock_was_lost so the caller
// can decide whether to push GPS / NTP time back into the chip.
esp_err_t pm_rtc_pcf8563_init(bool* clock_was_lost);

// Read the current calendar time.
esp_err_t pm_rtc_pcf8563_read(pm_rtc_time_t* out);

// Write the calendar time. Clears the VL flag — call this after
// a successful GPS or NTP fix.
esp_err_t pm_rtc_pcf8563_write(const pm_rtc_time_t* in);

// Convert to/from a POSIX time_t (UTC). Useful for syncing the
// system clock (settimeofday) from the RTC at boot.
time_t   pm_rtc_pcf8563_to_unix(const pm_rtc_time_t* in);
void     pm_rtc_pcf8563_from_unix(time_t t, pm_rtc_time_t* out);

// Push system time → RTC.
esp_err_t pm_rtc_pcf8563_sync_from_system(void);

// Push RTC → system time. Call once at boot before any timestamp-
// sensitive code (file writes, log lines) runs.
esp_err_t pm_rtc_pcf8563_sync_to_system(void);

#ifdef __cplusplus
}
#endif

#endif
