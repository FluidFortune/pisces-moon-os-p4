// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_C5_UART_H
#define PM_C5_UART_H

// ============================================================
//  pm_c5_uart.h — ESP32-C5 edge-radio peer over UART
//
//  PURPOSE
//  -------
//  Carries the same ASCII PMU1 protocol the Cardputer ADV uses,
//  but on a second UART (UART_NUM_2) connected to a Hosyond /
//  RockBase "NM-GTD-C5 Colorful" board running Pisces Moon Edge
//  (variant 6 of the OS).
//
//  The C5 is the project's dual-band edge radio. It brings two
//  things the C6 Ghost doesn't:
//    1. 5 GHz Wi-Fi 6 (the C6 is 2.4 GHz only).
//    2. A second, parallel 2.4 GHz radio so the device can
//       sniff and beacon at the same time without blocking
//       the C6's own scan loop.
//
//  WIRING (P4 side)
//  ----------------
//  The CrowPanel Advanced ESP32-P4 boards expose an "I2C1"
//  Grove connector wired to GPIO45/46. The ESP32-P4 GPIO matrix
//  is fully flexible, so we re-bind those pads to UART_NUM_2 at
//  runtime. The physical Grove I2C connector becomes a UART
//  connector for this peer only — no I2C transactions cross it
//  while pm_c5_uart is active.
//
//  Cable: SparkFun (or equivalent) QWIIC-to-Grove adapter — the
//  C5 side exposes only JST-SH 1.0 mm QWIIC connectors, and we
//  remap those pins on the C5 to UART as well.
//
//  CAUTION
//  -------
//  On the current BSP, GPIO45/46 are also routed to the GT911
//  touch controller. The board profile must move touch to a
//  different I2C peripheral (or different pins) before enabling
//  pm_c5_uart. The component refuses to start if PM_C5_UART_ENABLE
//  is not defined in the board profile, so a misconfigured build
//  doesn't silently break touch.
//
//  PROTOCOL
//  --------
//  Identical to pm_cardputer_i2c's UART bridge: ASCII lines
//  prefixed with "PMU1 ". See that header for the full line set.
//  The C5 peer adds a single capability bit:
//
//      PM_C5_CAP_WIFI_5GHZ = 1u << 7
//
//  Apps that want dual-band coverage request the peer with
//  cap "wifi_scan_5ghz" or "wifi_promisc_5ghz" — these resolve
//  to pm_c5_uart automatically via the peer registry.
// ============================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pm_cardputer_i2c.h"   // PMU1 data structs are shared

#ifdef __cplusplus
extern "C" {
#endif

// Capability extension specific to the C5. The lower seven bits
// of the caps bitmask carry the same meanings as the Cardputer's
// pm_cardputer_i2c_caps_t (KEYBOARD / GPS / LORA / WIFI / BLE /
// WIFI_PROMISC / WIFI_SCAN). Bit 7 is reserved for the C5's
// dual-band Wi-Fi 6 capability.
#define PM_C5_CAP_WIFI_5GHZ    (1u << 7)

// Default pin map (board-profile overridable). See the BSP note
// above before enabling this in production.
#ifndef PM_C5_UART_RX_PIN
#define PM_C5_UART_RX_PIN      45    // I2C1 Grove SDA pad, repurposed
#endif
#ifndef PM_C5_UART_TX_PIN
#define PM_C5_UART_TX_PIN      46    // I2C1 Grove SCL pad, repurposed
#endif
#ifndef PM_C5_UART_BAUD
#define PM_C5_UART_BAUD        921600
#endif

// ── Lifecycle ────────────────────────────────────────────────
// Start the UART bridge. Returns true if the driver came up; the
// link then watches for the C5's "PMU1 HELLO" event which unlocks
// capability registration. Safe to call multiple times — second
// call is a no-op once started.
bool pm_c5_uart_init(void);

// True once the UART driver is running. The peer is not yet
// "seen" — see pm_c5_uart_link_seen().
bool pm_c5_uart_present(void);

// True once the C5 has sent at least one PMU1 HELLO line. Apps
// gate dispatch on this so a never-connected C5 doesn't appear
// as a usable peer.
bool pm_c5_uart_link_seen(void);

// Current capability bitmask reported by the C5 (combines the
// pm_cardputer_i2c_caps_t bits plus the C5-only extension).
// Returns 0 until HELLO arrives.
uint32_t pm_c5_uart_caps(void);

// Number of UART bytes received since boot. Useful for the same
// "bytes arriving but no HELLO" diagnostic the Cardputer module
// surfaces.
uint32_t pm_c5_uart_rx_bytes(void);

// ── Outbound commands ────────────────────────────────────────
// These build the PMU1 CMD lines and write them to the C5. They
// are mirrors of the corresponding pm_cardputer_i2c functions,
// so apps can call either peer with the same op set via the
// pm_peer registry.
esp_err_t pm_c5_uart_ble_scan_start(bool active);
esp_err_t pm_c5_uart_ble_scan_stop(void);
esp_err_t pm_c5_uart_wifi_promisc_start(uint8_t channel, uint8_t filter);
esp_err_t pm_c5_uart_wifi_promisc_stop(void);
esp_err_t pm_c5_uart_wifi_set_channel(uint8_t channel);

// 5 GHz-specific helpers. These send the same "wifi_promisc_*"
// CMD lines but with a channel from the 5 GHz UNII bands (36, 40,
// ..., 165). The C5 picks the band from the channel number.
esp_err_t pm_c5_uart_wifi_promisc_start_5ghz(uint8_t channel, uint8_t filter);

// ── Inbound event queues ─────────────────────────────────────
// Drain queued events one at a time. The structs reuse the
// Cardputer types so consumers don't carry two parallel parsers.
// All return ESP_OK with .available == 0 when the queue is empty.
esp_err_t pm_c5_uart_wifi_frame_pop(pm_cardputer_i2c_wifi_frame_t* out);
esp_err_t pm_c5_uart_ble_seen_pop(pm_cardputer_i2c_ble_seen_t* out);
esp_err_t pm_c5_uart_lora_rx_pop(pm_cardputer_i2c_lora_rx_t* out);

// ── Peer-registry dispatcher ─────────────────────────────────
// Same op set as pm_cardputer_i2c_call(). Op accepts: ping,
// status, ble_scan_start, ble_scan_stop, ble_seen_pop,
// wifi_promisc_start, wifi_promisc_stop, wifi_set_channel,
// wifi_frame_pop, lora_*. Returns 0 on success, negative on
// transport/protocol failure.
int pm_c5_uart_call(const char* op, const char* params);

#ifdef __cplusplus
}
#endif

#endif  // PM_C5_UART_H
