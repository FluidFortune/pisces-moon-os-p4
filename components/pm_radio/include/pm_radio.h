// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_radio.h — Wireless module slot abstraction
//
//  The CrowPanel Advanced 7" has a slot that accepts any of
//  ELECROW's interchangeable wireless module carriers:
//    - SX1262         (LoRa, 868/915 MHz)         — implemented
//    - nRF24L01+      (2.4 GHz proprietary)       — implemented
//    - ESP32-H2       (Thread/Zigbee/Matter)      — declared, stub
//    - ESP32-C6 slot  (Wi-Fi 6 / BLE / 802.15.4)  — declared, stub
//    - Wi-Fi HaLow    (sub-GHz 802.11ah)          — declared, stub
//
//  All modules share the same SPI bus (CLK=8, MISO=7, MOSI=6)
//  and the same four control pins (NSS=10, BUSY/IRQ=9,
//  IRQ/CE=53, NRST/CS=54), reused with chip-specific semantics.
//
//  At init time pm_radio_init_auto() probes the slot:
//    1. Resets the chip via NRST line
//    2. Tries SX1262 GET_STATUS — if matches expected, → SX1262
//    3. Tries nRF24 STATUS register read — if reset value matches
//       and writes stick, → NRF24
//    4. Otherwise → NONE
//
//  H2/C6/HaLow are declared rather than auto-detected because
//  they are full MCUs running their own firmware; detection
//  depends on what's flashed and uses a different protocol
//  (UART handshake). For now those backends report NOT_IMPL.
//
//  Apps that need the radio:
//    pm_radio_kind_t k = pm_radio_init_auto();
//    if (k == PM_RADIO_SX1262) { mesh_app_enable(); }
//    else if (k == PM_RADIO_NRF24) { mousejack_app_enable(); }
//    else { show("No radio module detected"); }
//
//  The radio handle treaty is the same as before:
//    pm_radio_take(timeout_ms, who) — exclusive access
//    pm_radio_give()
//
//  Apps that share a slot module (e.g. mesh_messenger and
//  lora_voice for SX1262) take/give the handle to coordinate.
// ============================================================

#ifndef PM_RADIO_H
#define PM_RADIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_RADIO_NONE     =  0,
    PM_RADIO_SX1262   =  1,
    PM_RADIO_NRF24    =  2,
    PM_RADIO_ESP32_H2 =  3,    // declared; needs UART probe
    PM_RADIO_ESP32_C6 =  4,    // declared; needs UART probe
    PM_RADIO_HALOW    =  5,    // declared; needs UART probe
    PM_RADIO_UNKNOWN  = -1,
} pm_radio_kind_t;

// ── Pin map (shared across modules — verified ELECROW Lesson 14) ─
#define PM_RADIO_PIN_SPI_SCK    8
#define PM_RADIO_PIN_SPI_MISO   7
#define PM_RADIO_PIN_SPI_MOSI   6
// Slot control lines — semantics depend on detected module.
//   SX1262: NSS=10, BUSY=9, IRQ=53, NRST=54
//   nRF24:  CS=10,  IRQ=9,  CE=53,  (no nRST; device has internal POR)
#define PM_RADIO_PIN_CTL_A     10    // SX1262 NSS / nRF24 CS
#define PM_RADIO_PIN_CTL_B      9    // SX1262 BUSY / nRF24 IRQ
#define PM_RADIO_PIN_CTL_C     53    // SX1262 IRQ / nRF24 CE
#define PM_RADIO_PIN_CTL_D     54    // SX1262 NRST (unused on nRF24)

// ── Init / detection ────────────────────────────────────────
// Probes the slot and initializes whichever module is detected.
// Returns the detected kind. NONE means no module is plugged in
// (or it didn't respond to either probe). UNKNOWN means a probe
// failure that's likely a wiring issue, not a missing module.
pm_radio_kind_t pm_radio_init_auto(void);

// Force-init a specific kind (skips probing). Use when you know
// what's plugged in — e.g. user-declared H2 or C6 modules.
esp_err_t pm_radio_init_as(pm_radio_kind_t kind);

// Currently-active module kind, or NONE if init hasn't run yet.
pm_radio_kind_t pm_radio_kind(void);

// Human-readable name for logging/UI.
const char* pm_radio_name(pm_radio_kind_t kind);

// ── Treaty (exclusive radio handle) ─────────────────────────
// Apps sharing a module call _take before driving and _give
// when done. timeout_ms = 0xFFFFFFFFu blocks forever.
bool pm_radio_take(uint32_t timeout_ms, const char* who);
void pm_radio_give(void);

// ── TX/RX (only meaningful for SPI-attached modules) ─────────
// For SX1262: classic LoRa packet. For nRF24: payload up to 32B.
// For H2/C6/HaLow: returns PM_RADIO_NOT_IMPL.
typedef enum {
    PM_RADIO_OK         =  0,
    PM_RADIO_ERR        = -1,
    PM_RADIO_NOT_INIT   = -2,
    PM_RADIO_BUSY       = -3,
    PM_RADIO_NOT_IMPL   = -4,    // backend doesn't support this op
    PM_RADIO_TIMEOUT    = -5,
    PM_RADIO_TX_FAIL    = -6,
    PM_RADIO_RX_FAIL    = -7,
    PM_RADIO_BAD_PARAM  = -8,
} pm_radio_status_t;

typedef void (*pm_radio_rx_cb_t)(const uint8_t* buf, size_t len,
                                   int rssi, float snr, void* user);

pm_radio_status_t pm_radio_set_rx_cb(pm_radio_rx_cb_t cb, void* user);
pm_radio_status_t pm_radio_tx(const uint8_t* buf, size_t len,
                                uint32_t timeout_ms);

// ── Diagnostics ─────────────────────────────────────────────
// Useful for the SYSTEM/about-radio screen. Each backend may
// fill different fields; missing ones come back as zero or NULL.
typedef struct {
    pm_radio_kind_t kind;
    bool            initialized;
    int             last_rssi;
    float           last_snr;
    uint32_t        tx_count;
    uint32_t        rx_count;
    const char*     mode_str;   // backend-specific (e.g. "LongFast", "ShockBurst")
} pm_radio_info_t;

void pm_radio_info(pm_radio_info_t* out);

#ifdef __cplusplus
}
#endif

#endif  // PM_RADIO_H
