// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_lora.h — SX1262 driver for the P4 wireless module slot
//
//  Wraps the jgromes/radiolib C++ library behind a clean C
//  API. Apps see:
//    - pm_lora_init()           — one-time radio bring-up
//    - pm_lora_set_mode_voice() — FSK 8kHz voice, 44-byte frames
//    - pm_lora_set_mode_mesh()  — Meshtastic LongFast preset
//    - pm_lora_tx(buf, len)     — blocking TX
//    - pm_lora_set_rx_cb(cb)    — async RX callback
//    - pm_lora_set_freq_mhz(f)  — channel select
//
//  Mutex discipline: voice and mesh apps cannot run at the
//  same time. pm_lora_take(timeout) and pm_lora_give() are
//  the radio-handle treaty. Apps that hold the handle have
//  exclusive access; the other app's TX call returns
//  PM_LORA_BUSY immediately.
//
//  This file deliberately avoids exposing radiolib types so
//  the wrapper can swap to nopnop2002/esp-idf-sx126x or to
//  the ESP-Hosted LoRa stack later without touching apps.
// ============================================================

#ifndef PM_LORA_H
#define PM_LORA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_LORA_OK         = 0,
    PM_LORA_ERR        = -1,
    PM_LORA_NOT_INIT   = -2,
    PM_LORA_BUSY       = -3,    // someone else holds the handle
    PM_LORA_BAD_MODE   = -4,
    PM_LORA_TIMEOUT    = -5,
    PM_LORA_TX_FAIL    = -6,
    PM_LORA_RX_FAIL    = -7,
} pm_lora_status_t;

typedef enum {
    PM_LORA_MODE_VOICE = 0,    // 2-FSK, 100 kbps, 50 kHz dev (Codec2 carrier)
    PM_LORA_MODE_MESH  = 1,    // Meshtastic LongFast: SF11 BW250 CR4/8 sync 0x2B
} pm_lora_mode_t;

// Async receive callback. Called on the radio worker task.
// Buffer is borrowed for the call duration only; copy if you
// need it longer.
typedef void (*pm_lora_rx_cb_t)(const uint8_t* buf, size_t len,
                                  int rssi, float snr, void* user);

// ── Init / status ───────────────────────────────────────────
pm_lora_status_t pm_lora_init(void);
bool             pm_lora_is_initialized(void);

// Pin map (VERIFIED ELECROW Lesson 14). All pins are
// for the P4's wireless module slot. Note that NSS/BUSY/IRQ/RST
// are SHARED with the nRF24 carrier (NRF24_CS / NRF24_IRQ /
// NRF24_CE) — the same physical pins, just different chip
// semantics. The radio detector reads chip-specific signatures
// to decide which is plugged in.
#define PM_LORA_PIN_NSS    10    // SX1262 NSS  / nRF24 CS
#define PM_LORA_PIN_BUSY    9    // SX1262 BUSY / nRF24 IRQ
#define PM_LORA_PIN_DIO1   53    // SX1262 IRQ  / nRF24 CE
#define PM_LORA_PIN_RST    54    // SX1262 NRST
// SPI pins inherited from pm_hal SPI Treaty bus
// (CLK=8, MISO=7, MOSI=6 per ELECROW).

// ── Mode selection ──────────────────────────────────────────
// Switching modes drops any in-flight TX/RX and reconfigures
// the radio. Returns PM_LORA_OK on success.
pm_lora_status_t pm_lora_set_mode_voice(void);
pm_lora_status_t pm_lora_set_mode_mesh (void);
pm_lora_mode_t   pm_lora_current_mode  (void);

// ── Treaty (exclusive radio handle) ─────────────────────────
// Voice / mesh / future apps that share the radio call _take
// before driving and _give when done. Other apps' TX/RX calls
// return PM_LORA_BUSY when the handle is held by someone else.
//
// timeout_ms = 0 → non-blocking try-take
// timeout_ms = portMAX_DELAY-equivalent → block forever (use 0xFFFFFFFFu)
bool pm_lora_take(uint32_t timeout_ms, const char* who);
void pm_lora_give(void);

// ── Frequency selection ─────────────────────────────────────
// MHz with fractional resolution to MHz/1000. e.g. 906875000
// for 906.875 MHz LongFast US default. Set BEFORE TX/RX.
pm_lora_status_t pm_lora_set_freq_mhz(float mhz);

// ── TX/RX ───────────────────────────────────────────────────
// Blocking TX. Takes the SPI bus internally; do not call
// while holding it. timeout_ms applies to TX completion.
pm_lora_status_t pm_lora_tx(const uint8_t* buf, size_t len, uint32_t timeout_ms);

// Set the RX callback. NULL disables RX. The driver enters
// continuous RX mode whenever a callback is set.
pm_lora_status_t pm_lora_set_rx_cb(pm_lora_rx_cb_t cb, void* user);

// Diagnostics
int   pm_lora_last_rssi(void);
float pm_lora_last_snr (void);

#ifdef __cplusplus
}
#endif

#endif  // PM_LORA_H
