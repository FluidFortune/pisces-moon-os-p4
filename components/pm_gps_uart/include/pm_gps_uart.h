// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_gps_uart.h — P4-direct GPS reader
//
//  Reads NMEA from the BN-180 GPS module wired to the P4's
//  2×12 GPIO header (left strip pins 5/6 = IO2/IO3). Parses
//  $GPRMC and $GPGGA sentences, feeds pm_gps_state directly.
//
//  Architecture rationale:
//    Originally GPS was routed via the C6 Ghost Engine, which
//    parsed NMEA and shipped fixes over the bridge UART. With
//    Eric's actual board the GPS sits on the P4 side, so we
//    skip the C6 hop entirely. The C6 stays focused on radio
//    work; GPS is independent of C6 firmware status.
//
//  Boot sequence:
//    1. pm_gps_uart_init()  — UART setup, task spawn
//    2. Task reads from UART, parses sentences as they arrive
//    3. Each valid fix calls pm_gps_state_update_*()
//    4. App layer reads via the existing pm_gps_state API
//
//  Failure modes (all handled gracefully):
//    - GPS not connected → no sentences → state stays "no fix"
//    - Wrong baud rate → garbage in → checksum mismatch → drop
//    - Module powered off → returns happily, just no data
// ============================================================

#ifndef PM_GPS_UART_H
#define PM_GPS_UART_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Pin map (ELECROW DHE04107D, 2×12 GPIO header left strip) ─
// Confirmed by Eric from the silkscreen photo:
//   pin 1 = 3V3   (GPS VCC)
//   pin 4 = GND
//   pin 5 = IO2   (GPS TX → P4 RX)
//   pin 6 = IO3   (P4 TX → GPS RX, used only for AT config)
#define PM_GPS_UART_NUM     1            // P4 UART1 (free per board map)
#define PM_GPS_PIN_RX       2
#define PM_GPS_PIN_TX       3
#define PM_GPS_BAUD         9600         // BN-180 default

// Init UART, register sentences with parser, spawn read task.
esp_err_t pm_gps_uart_init(void);

// Stats — useful for diagnostics screens.
typedef struct {
    uint32_t bytes_rx;       // raw bytes received
    uint32_t sentences_seen; // NMEA sentences with valid checksum
    uint32_t sentences_bad;  // checksum failed or malformed
    uint32_t fixes_valid;    // RMC sentences with status='A'
    uint32_t fixes_invalid;  // RMC sentences with status='V'
} pm_gps_uart_stats_t;

void pm_gps_uart_stats(pm_gps_uart_stats_t* out);

// Optional: send a raw command to the GPS (e.g. PMTK config).
// Most users won't need this — BN-180 ships in a sane state.
void pm_gps_uart_send_cmd(const char* cmd);

#ifdef __cplusplus
}
#endif

#endif  // PM_GPS_UART_H
