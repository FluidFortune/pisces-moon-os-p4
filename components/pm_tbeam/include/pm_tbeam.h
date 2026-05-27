// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_tbeam.h — LilyGO T-Beam Supreme S3 secondary radio peer
//
//  The T-Beam Supreme S3 is an OPTIONAL ESP32-S3 board running
//  "Pisces Moon Peer" firmware (defined in pisces-moon-tbeam-s3/,
//  shipped separately). When wired to the CrowPanel's 2x12
//  header (RXD=IO25, TXD=IO27, GND), it becomes a SECONDARY
//  radio peer that runs PARALLEL to the C6:
//
//    - SECONDARY WiFi scan/sniff   (C6 keeps wardriving)
//    - SECONDARY BLE scan/GATT     (C6 keeps wardriving)
//    - PRIMARY LoRa (when no wireless-slot module installed)
//    - GPS disabled (BN-180 on P4 is canonical source)
//
//  The T-Beam is MODULAR. If it's not wired, the OS notices
//  silently and apps that asked for "secondary WiFi" simply
//  return PEER_UNAVAILABLE. Nothing breaks.
//
//  Bridge protocol over UART:
//    Baud:    921600
//    Format:  one JSON object per line, terminated with '\n'
//    Pins:    P4 IO25 (RX from T-Beam TX), P4 IO27 (TX to T-Beam RX)
//
//  Events from T-Beam (T-Beam → P4):
//    {"event":"tbeam_ready","fw_version":"...","caps":["wifi_scan",...]}
//    {"event":"tbeam_wifi_seen", ...}        same shape as C6 wifi_seen
//    {"event":"tbeam_ble_seen", ...}
//    {"event":"tbeam_lora_rx","data":"<hex>","rssi":N,"snr":N}
//    {"event":"tbeam_status","heap":N,"uptime_s":N}
//
//  Commands to T-Beam (P4 → T-Beam):
//    {"cmd":"wifi_scan"}
//    {"cmd":"wifi_promisc_start","channel":N}
//    {"cmd":"wifi_promisc_stop"}
//    {"cmd":"ble_scan_start"}
//    {"cmd":"ble_scan_stop"}
//    {"cmd":"lora_tx","data":"<hex>"}
//    {"cmd":"lora_rx_arm"}
//    {"cmd":"ping"}
// ============================================================

#ifndef PM_TBEAM_H
#define PM_TBEAM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_TBEAM_UART_NUM   2
#define PM_TBEAM_BAUD       921600
#define PM_TBEAM_PIN_RX     25      // P4 receives T-Beam's TX
#define PM_TBEAM_PIN_TX     27      // P4 transmits to T-Beam's RX
#define PM_TBEAM_HEARTBEAT_MS 5000  // expect tbeam_status this often

// ── Lifecycle ───────────────────────────────────────────────
//
// Returns true if a T-Beam was detected during the probe
// window (~3 seconds, waiting for a tbeam_ready event after
// sending a ping). If not detected, the OS continues silently
// without the peer.
bool pm_tbeam_init_auto(void);

// Manual control (used by pm_peer reconnection logic)
void pm_tbeam_send_cmd(const char* json_line);
bool pm_tbeam_connected(void);

// ── Events forwarded to the peer registry ───────────────────
//
// The pm_tbeam UART task parses incoming JSON lines and
// announces wifi_seen / ble_seen / lora_rx events through the
// peer registry's event bus. Apps subscribe to the bus
// (pm_peer_subscribe) and receive both C6 and T-Beam events
// uniformly tagged by source peer.

// ── Direct send API (for apps holding a peer handle) ────────
typedef enum {
    PM_TBEAM_OP_WIFI_SCAN          = 0,
    PM_TBEAM_OP_WIFI_PROMISC_START = 1,
    PM_TBEAM_OP_WIFI_PROMISC_STOP  = 2,
    PM_TBEAM_OP_BLE_SCAN_START     = 3,
    PM_TBEAM_OP_BLE_SCAN_STOP      = 4,
    PM_TBEAM_OP_LORA_TX            = 5,
    PM_TBEAM_OP_LORA_RX_ARM        = 6,
    PM_TBEAM_OP_PING               = 7,
} pm_tbeam_op_t;

int pm_tbeam_do(pm_tbeam_op_t op, const char* json_args);

#ifdef __cplusplus
}
#endif

#endif  // PM_TBEAM_H
