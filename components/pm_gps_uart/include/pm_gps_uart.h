// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_gps_uart.h — P4-direct GPS reader
//
//  Two roles depending on board:
//    - Elecrow CrowPanel P4 (5"/7"): optional bench path for a
//      BN-180 module wired directly to GPIO 52 on UART4. Normal
//      builds prefer the Cardputer ADV UART1 header bridge.
//      Gated on PM_BOARD_LOCAL_GPS_UART=1 in the board profile.
//
//    - LilyGO T-Display-P4: native L76K module wired on UART3
//      at GPIO 22/23 @ 9600 baud. Always on; PM_BOARD_LOCAL_GPS_UART
//      defaults to 1 in that profile.
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
#include "pm_board.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Pin map (board-conditional) ─────────────────────────────────────
#if defined(PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4)
  // L76K native on UART3, GPIO 22/23, 9600 baud (factory default).
  // UART_NUM_2 is reserved for pm_c5_uart on this board.
  #define PM_GPS_UART_NUM   3
  #define PM_GPS_PIN_RX     PM_BOARD_LOCAL_GPS_RX_PIN
  #define PM_GPS_PIN_TX     PM_BOARD_LOCAL_GPS_TX_PIN
  #define PM_GPS_BAUD       PM_BOARD_LOCAL_GPS_BAUD
#else
  // Elecrow bench path: BN-180 on GPIO 52 (5V row), UART4, RX-only.
  #define PM_GPS_UART_NUM   4
  #define PM_GPS_PIN_RX     52
  #define PM_GPS_PIN_TX     (-1)
  #define PM_GPS_BAUD       9600
#endif

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
