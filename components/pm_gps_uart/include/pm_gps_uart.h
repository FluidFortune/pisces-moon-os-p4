// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_gps_uart.h — P4-direct GPS reader (parked)
//
//  Optional bench path for a BN-180 GPS module wired directly to
//  the P4. Normal board profiles use the Cardputer ADV UART1 header
//  bridge for GPS/radio/keyboard expansion instead.
//
//  Architecture note:
//    The IO52 local GPS experiment stays in-tree on UART4 so it can
//    be re-enabled by setting PM_BOARD_LOCAL_GPS_UART=1 in the active
//    board profile. It is intentionally disabled for standard builds.
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

// ── Pin map ─────────────────────────────────────────────────
// Current hardware test rig powers the BN-180 from 5V because this
// Beitian variant appears unstable from the CrowPanel 3V3 rail.
// Moving to the 5V row shifts the GPS TX lead to IO52 on Eric's board.
// GPS is used receive-only; there is no paired TX/probe pin in this setup.
#define PM_GPS_UART_NUM     4            // P4 UART4; UART1 belongs to Cardputer
#define PM_GPS_PIN_RX       52
#define PM_GPS_PIN_TX       (-1)
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
    uint8_t  active_rx_pin;  // current UART RX GPIO
    bool     using_swapped_pins;
    uint32_t active_baud;    // current UART baud
} pm_gps_uart_stats_t;

void pm_gps_uart_stats(pm_gps_uart_stats_t* out);

// Optional: send a raw command to the GPS (e.g. PMTK config).
// Most users won't need this — BN-180 ships in a sane state.
void pm_gps_uart_send_cmd(const char* cmd);

#ifdef __cplusplus
}
#endif

#endif  // PM_GPS_UART_H
